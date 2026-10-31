#include <thread>
#include <map>
#include <set>
#include <numeric>
#include "genie_network.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/Common.hh"
#include "astra-sim/system/WorkloadLayerHandlerData.hh"
#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/common/Common.hh"
#include <chrono>
#include <x86intrin.h>
#include <json/json.hpp>

#ifdef GENIE_CHROMETRACE_EVENT
static inline uint64_t rdtscp_intrinsic(void) {
    unsigned int aux;
    return __rdtscp(&aux);
}
#endif

using json=nlohmann::json;

int GeniePairChannelLayout::channel_of(const std::vector<int>& sorted_members, int rank, int peer) const {
    int channel = 0;
    for (const auto& set : member_sets) {
        if (!std::binary_search(set.begin(), set.end(), rank) || !std::binary_search(set.begin(), set.end(), peer)) {
            continue;
        }
        if (set == sorted_members) {
            return channel;
        }
        channel++;
    }
    throw std::runtime_error("GeniePairChannelLayout: member set of rank " + std::to_string(rank) +
                             " with peer " + std::to_string(peer) + " is not a scale-out member set");
}

GeniePairChannelLayout genie_pair_channel_layout(const std::string& comm_group_filepath, int world_size) {
    GeniePairChannelLayout layout;
    const bool only_scaleout = AstraSim::env_var_is_true("GENIE_ONLY_SCALEOUT");
    std::set<std::vector<int>> sets;
    if (comm_group_filepath.find("empty") != std::string::npos) {
        std::vector<int> all_ranks(world_size);
        std::iota(all_ranks.begin(), all_ranks.end(), 0);
        sets.insert(all_ranks);
    } else {
        std::ifstream inFile(comm_group_filepath);
        json j;
        inFile >> j;
        const size_t num_groups = j.size();
        for (json::iterator it = j.begin(); it != j.end(); ++it) {
            const json& ranks_json = it.value().is_object() ? it.value().at("ranks") : it.value();
            std::vector<int> members;
            for (auto id : ranks_json) {
                members.push_back(id);
            }
            if (members.empty()) {
                continue;
            }
            int group_id = std::stoi(it.key());
            // Same rules as should_skip_qp_init_for_comm_group.
            if (num_groups > 1 && group_id == 0) {
                continue;
            }
            AstraSim::CommunicatorGroup cg(group_id, members, members[0]);
            cg.set_only_scaleout(only_scaleout);
            if (it.value().is_object() && it.value().contains("scale_up")) {
                cg.set_scale_up(it.value().at("scale_up").get<bool>());
            }
            if (cg.is_scale_up_domain()) {
                continue;
            }
            std::sort(members.begin(), members.end());
            members.erase(std::unique(members.begin(), members.end()), members.end());
            sets.insert(members);
        }
    }
    layout.member_sets.assign(sets.begin(), sets.end());
    std::map<std::pair<int, int>, int> pair_count;
    for (const auto& set : layout.member_sets) {
        for (size_t a = 0; a < set.size(); a++) {
            for (size_t b = a + 1; b < set.size(); b++) {
                int c = ++pair_count[{set[a], set[b]}];
                layout.num_channels = std::max(layout.num_channels, c);
            }
        }
    }
    return layout;
}

std::vector<AstraSim::CommunicatorGroup*> ASTRASimGenieNetwork::initialize_comm_group(std::string comm_group_filepath) {
    std::vector<AstraSim::CommunicatorGroup*> comm_groups;
    const bool only_scaleout = AstraSim::env_var_is_true("GENIE_ONLY_SCALEOUT");
    // communicator group input file is not given: create a default all-ranks group (id=0)
    if (comm_group_filepath.find("empty") != std::string::npos) {
        std::vector<int> all_ranks(_context->size);
        std::iota(all_ranks.begin(), all_ranks.end(), 0);
        auto* comm_group = new AstraSim::CommunicatorGroup(0, all_ranks, rank);
        comm_group->set_only_scaleout(only_scaleout);
        comm_groups.push_back(comm_group);
        _logger->info("no comm_group file provided. Created default all-ranks comm group (size={})", _context->size);
        return comm_groups;
    }

    std::ifstream inFile(comm_group_filepath);
    json j;
    inFile >> j;
    num_file_comm_groups = static_cast<int>(j.size());

    for (json::iterator it = j.begin(); it != j.end(); ++it) {
        std::vector<int> involved_NPUs;
        // Either a bare rank array (legacy) or {"ranks": [...], "scale_up": bool}.
        const json& ranks_json = it.value().is_object() ? it.value().at("ranks") : it.value();
        for (auto id : ranks_json) {
            involved_NPUs.push_back(id);
        }
        // Skip groups that don't include this rank
        if (std::find(involved_NPUs.begin(), involved_NPUs.end(), rank) == involved_NPUs.end()) {
            continue;
        }
        int group_id = std::stoi(it.key());
        auto* comm_group = new AstraSim::CommunicatorGroup(group_id, involved_NPUs, rank);
        comm_group->set_only_scaleout(only_scaleout);
        if (it.value().is_object() && it.value().contains("scale_up")) {
            comm_group->set_scale_up(it.value().at("scale_up").get<bool>());
        }
        comm_groups.push_back(comm_group);
    }
    return comm_groups;
}

bool ASTRASimGenieNetwork::should_skip_qp_init_for_comm_group(AstraSim::CommunicatorGroup* comm_group, int num_comm_groups) {
    if (comm_group == nullptr) {
        throw std::runtime_error("Comm group is null. This should only happen when the input comm group file is empty, which should have been handled in initialize_comm_group.");
    }

    // Use the file-wide group count so every rank agrees on which groups get QPs.
    if (std::max(num_comm_groups, num_file_comm_groups) > 1 && comm_group->get_id() == 0) {
        _logger->debug("Rank {}: skipping comm group {} since it is a placeholder all-node group.", rank, comm_group->get_id());
        return true;
    }

    if (comm_group->is_scale_up_domain()) {
        return true;
    }

    return false;
}

std::unordered_map<int, QueuepairManager*> ASTRASimGenieNetwork::initialize_qp_managers(std::vector<AstraSim::CommunicatorGroup*> comm_groups, int nqps) {
    std::unordered_map<int, QueuepairManager*> qp_managers;
    // Map from sorted member set to an already-created QPManager, to share across groups with identical members.
    std::map<std::vector<int>, QueuepairManager*> members_to_qpm;
    int num_comm_groups = comm_groups.size();
    for (auto comm_group : comm_groups) {
        if (should_skip_qp_init_for_comm_group(comm_group, num_comm_groups)) {
            _logger->debug("Rank {}: skipping comm group {}", rank, comm_group->get_id());
            continue;
        }
        std::vector<int> members = comm_group->involved_NPUs;
        // Only initialize QP manager if this rank is a member of the group.
        bool rank_in_group = std::find(members.begin(), members.end(), rank) != members.end();
        if (!rank_in_group) {
            _logger->debug("Rank {}: skipping comm group {} since rank is not a member", rank, comm_group->get_id());
            continue;
        }

        std::vector<int> sorted_members = members;
        std::sort(sorted_members.begin(), sorted_members.end());

        int group_id = comm_group->get_id();
        if (members_to_qpm.count(sorted_members)) {
            // Reuse existing QPManager for groups with identical members.
            qp_managers[group_id] = members_to_qpm[sorted_members];
            _logger->debug("Rank {}: reusing QP manager for comm group {} of size {}", rank, group_id, members.size());
        } else {
            // gloo pair index base per peer: this member set's channel for (rank, peer).
            int max_peer = *std::max_element(sorted_members.begin(), sorted_members.end());
            std::vector<int> pair_base_by_peer(max_peer + 1, 0);
            for (int peer : sorted_members) {
                if (peer != rank) {
                    pair_base_by_peer[peer] = pair_channel_layout.channel_of(sorted_members, rank, peer) * nqps;
                }
            }
            QueuepairManager* qpm = new QueuepairManager(_context->transportContext_, _logger, rank, nqps, comm_group->involved_NPUs, event_queue, group_id, pair_base_by_peer, nqps * pair_channel_layout.num_channels);
            qp_managers[group_id] = qpm;
            members_to_qpm[sorted_members] = qpm;
        }
    }
    return qp_managers;
}

ASTRASimGenieNetwork::ASTRASimGenieNetwork(int rank, std::shared_ptr<gloo::Context> context, AstraSim::ChromeTracer* chrome_tracer, int nqps, std::string comm_group_filepath)
    : AstraSim::AstraNetworkAPI(rank), _context(context), chrome_tracer(chrome_tracer), _schedule_poll_counter(0), genie_collective_ptr(nullptr) {
        threadcounter = new Threadcounter();
        timekeeper = new Timekeeper();
        _logger = AstraSim::LoggerFactory::get_logger("genie::frontend");
        comm_groups = initialize_comm_group(comm_group_filepath);
        pair_channel_layout = genie_pair_channel_layout(comm_group_filepath, _context->size);
        _logger->info("Rank {}: {} scale-out member sets, {} gloo pair channel(s) per rank pair",
                      rank, pair_channel_layout.member_sets.size(), pair_channel_layout.num_channels);
        // TODO: This assumes a ring collective of contiguous NPUs.
        event_queue = new EventQueue(this);
#ifndef REPLAY_CPU_ONLY
        qp_managers = initialize_qp_managers(comm_groups, nqps);
#endif
        sim_send_args = new RingTrain<SimSendArgs>(64 * nqps, 0);
        sim_recv_args = new RingTrain<SimRecvArgs>(64 * nqps, 1);
    }

void ASTRASimGenieNetwork::dump_qp_stats() {
    if (!genie_qpstats::enabled()) {
        return;
    }
    std::set<QueuepairManager*> seen;
    for (auto& kv : qp_managers) {
        QueuepairManager* qpm = kv.second;
        if (qpm == nullptr || !seen.insert(qpm).second) continue;
        for (size_t i = 0; i < qpm->stats.size(); i++) {
            auto& st = qpm->stats[i];
            if (!st.used) continue;
            int peer = (int)i / qpm->nqps, qp = (int)i % qpm->nqps;
            const double sm = st.s_msgs ? (double)st.s_msgs : 1.0;
            _logger->info(
                "[QPSTATS] rank={} group={} peer={} qp={} SEND msgs={} bytes={} inflight_avg={:.2f} inflight_max={} "
                "inflight_MiB_avg={:.2f} inflight_MiB_max={:.2f} post_idle={} idle_ms={:.3f} cts_first={} cts_spins={} "
                "cts_wait_us={} lat_us={} cqe_gap_us={} post_cost_us={} | RECV msgs={} inflight_max={} lat_us={} cqe_gap_us={} "
                "(us: avg/p50/p90/p99/max)",
                rank, qpm->comm_group_id, peer, qp, st.s_msgs, st.s_bytes, st.s_inflight_sum / sm, st.s_inflight_max,
                st.s_inflight_bytes_sum / sm / 1048576.0, st.s_inflight_bytes_max / 1048576.0, st.s_post_idle,
                st.s_idle_ns / 1e6, st.s_cts_first, st.s_cts_spins, st.cts_wait.us(), st.s_lat.us(), st.s_cqe_gap.us(),
                st.post_cost.us(), st.r_msgs, st.r_inflight_max, st.r_lat.us(), st.r_cqe_gap.us());
        }
    }
    auto& L = genie_qpstats::loop();
    const double it = L.iters ? (double)L.iters : 1.0;
    _logger->info(
        "[QPSTATS-LOOP] rank={} loop_ms={:.3f} iters={} ns_per_iter={:.1f} qlen_avg={:.2f} poll_send={} hit={} "
        "poll_recv={} hit={} send_cqes={} recv_cqes={} cqes_per_hit_avg={:.2f} max={} sim_send={} sim_send_miss={} "
        "sim_recv={} sched={}",
        rank, L.ns / 1e6, L.iters, L.ns / it, L.qlen_sum / it, L.poll_send, L.poll_send_hit, L.poll_recv,
        L.poll_recv_hit, L.send_cqes, L.recv_cqes, L.cqes_per_hit.avg(), L.cqes_per_hit.mx, L.sim_send,
        L.sim_send_miss, L.sim_recv, L.sched);
    auto& T = genie_qpstats::tracer();
    if (!T.recs.empty()) {
        const char* dir = getenv("GENIE_QP_TRACE_DIR");
        if (dir == nullptr) dir = getenv("OUTPUT_PATH");
        std::string d = std::string(dir ? dir : ".") + "/qptrace";
        std::string cmd = "mkdir -p '" + d + "'";
        if (system(cmd.c_str()) != 0) {
            _logger->warn("[QPSTATS] cannot create {}", d);
        }
        std::string f = d + "/rank" + std::to_string(rank) + ".csv";
        FILE* fp = fopen(f.c_str(), "w");
        if (fp) {
            fprintf(fp, "t_ns,type,peer,qp,seq\n");
            static const char* names[] = {"ready", "post", "scqe", "rpost", "rcqe"};
            for (auto& r : T.recs) {
                fprintf(fp, "%llu,%s,%u,%u,%u\n", (unsigned long long)r.t, names[r.type], r.peer, r.qp, r.seq);
            }
            fclose(fp);
            _logger->info("[QPSTATS] wrote {} trace events to {}", T.recs.size(), f);
        }
    }
}

ASTRASimGenieNetwork::~ASTRASimGenieNetwork() {
    dump_qp_stats();
    delete event_queue;
    std::set<QueuepairManager*> deleted_qpms;
    for (auto& pair : qp_managers) {
        if (deleted_qpms.insert(pair.second).second) {
            delete pair.second;
        }
    }
    delete timekeeper;
    delete threadcounter;
    delete sim_send_args;
    delete sim_recv_args;
}

void ASTRASimGenieNetwork::sim_notify_finished() {
    event_queue->mark_workload_finished();
}

AstraSim::timespec_t ASTRASimGenieNetwork::sim_get_time() {
    AstraSim::timespec_t timeSpec;
    timeSpec.time_res = AstraSim::NS;
    timeSpec.time_val = timekeeper->elapsedTimeNanoseconds();
    return timeSpec;
}

void ASTRASimGenieNetwork::sim_schedule(AstraSim::timespec_t delta,
                                       AstraSim::Callable* callable,
                                       AstraSim::EventType event_type,
                                       AstraSim::CallData* callData) {
    auto start_time = std::chrono::steady_clock::now();
    bool can_cast = true;
    AstraSim::WorkloadLayerHandlerData* wlhd = nullptr;
    wlhd = dynamic_cast<AstraSim::WorkloadLayerHandlerData*>(callData);
    std::string event_name = "";
    int event_id = 0;
    bool is_gpu = false;
    if (wlhd) {
        can_cast = false;
        event_name = wlhd->name;
        event_id = wlhd->node_id;
        is_gpu = wlhd->is_gpu;
    }
    SimScheduleArgs *event_args = new SimScheduleArgs {
        -1,
        -1,
        delta,
        callable,
        event_type,
        callData,
        start_time,
        event_name,
        event_id,
        is_gpu
    };
    Event event(SCHEDULE_EVENT, event_args);
    // std::cout << "At timestamp " << sim_get_time().time_val << ", scheduling event " << event_name << " with delta " << delta.time_val << " and resolution " << delta.time_res << std::endl;
    event_queue->add_event(event);

    // sim_schedule is largely called for 1) Compute operations, 2) Reduce computations, and 3) Modelling data movement between NPU and MA. 
    // For now, we return immediately (to avoid making debugging etc complicated). i.e. we do not model the above compute operations.
    // TODO: Revive the code below. 
    #ifdef FALSE
    //auto start_time = timekeeper->elapsedTimeNanoseconds();
    threadcounter->IncrementThreadCount();
    //auto increment_time = timekeeper->elapsedTimeNanoseconds();
    pthread_t thread;
    struct ThreadArgs {
        AstraSim::timespec_t delta;
        AstraSim::Callable* callable;
        AstraSim::EventType event;
        AstraSim::CallData* callData;
        Threadcounter* threadcounter;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);
        //auto sleep_start_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        if (threadArgs->delta.time_res == AstraSim::NS) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        } else if (threadArgs->delta.time_res == AstraSim::US) {
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        } else if (threadArgs->delta.time_res == AstraSim::MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        }
        //auto sleep_end_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        _logger->debug("Sim_Schedule with sleep_time {} and resolution {}", threadArgs->delta.time_val, threadArgs->delta.time_res);
        //<< " start_time " << threadArgs->start_time << " increment_time " << threadArgs->increment_time << " sleep_start_time " << sleep_start_time  << " sleep_end_time " << sleep_end_time << std::endl;
        threadArgs->callable->call(threadArgs->event, threadArgs->callData);
        threadArgs->threadcounter->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{delta, callable, event, callData, threadcounter};
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return;
    #endif
}

int ASTRASimGenieNetwork::sim_send(void* buffer,
                                  uint64_t msg_size,
                                  int type,
                                  int dst_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  int comm_group_id,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    // TODO: The buffer index and the QP is hardcoded here. 
    // int send_buf_idx = threadArgs->send_buf_idx;
    int qp_idx = tag;
    auto qp_manager = qp_managers[comm_group_id];
    auto buf = qp_manager->send_buffers[dst_id * qp_manager->nqps + qp_idx];

    SimSendArgs *event_args = sim_send_args->get_slot_to_write();
    event_args->stream_id = request->tag;
    event_args->peer_rank = dst_id;
    event_args->qp_idx = qp_idx;
    event_args->network = this;
    event_args->buf = buf;
    event_args->msg_size = msg_size;
    event_args->msg_handler = msg_handler;
    event_args->fun_arg = fun_arg;
    event_args->event_queue = event_queue;
    event_args->comm_group_id = comm_group_id;
    event_args->spins = 0;
    if (genie_qpstats::enabled()) {
        event_args->t_ready = genie_qpstats::now_ns();
        genie_qpstats::tracer().rec(genie_qpstats::T_READY, dst_id, qp_idx, request->tag, event_args->t_ready);
    }

    Event event(SIM_SEND, event_args);
    event_queue->add_event(event);
    

    // TODO: The message size is fixed to the buffer size. 
    // Must send only 'message_size' bytes.
    
    // buf->waitSend();
    // long long send_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
    //                     std::chrono::system_clock::now().time_since_epoch())
    //                     .count();
    //threadArgs->logger->debug("Send Complete");
    return 0;
}

int ASTRASimGenieNetwork::sim_recv(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int src_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  int comm_group_id,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    // long long recv_start_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
    //                     std::chrono::system_clock::now().time_since_epoch())
    //                     .count();
    // TODO: The buffer index and the QP is hardcoded here. 
    int qp_idx = tag;
    auto qp_manager = qp_managers[comm_group_id];
    auto buf = qp_manager->recv_buffers[src_id * qp_manager->nqps + qp_idx];
    SimRecvArgs *event_args = sim_recv_args->get_slot_to_write();
    event_args->stream_id = request->tag;
    event_args->peer_rank = src_id;
    event_args->qp_idx = qp_idx;
    event_args->buf = buf;
    event_args->msg_handler = msg_handler;
    event_args->fun_arg = fun_arg;
    event_args->event_queue = event_queue;
    event_args->comm_group_id = comm_group_id;
    Event event(SIM_RECV, event_args);
    event_queue->add_event(event);
    // TODO: Does it make sense not to create a thread here, when waitSend is in a detached thread?
    // buf->waitRecv();
    // long long recv_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
    //                     std::chrono::system_clock::now().time_since_epoch())
    //                     .count();
    //logger->debug("Recv from {} to of size {} at time {}", src_id, message_size, recv_start_time);
    //logger->debug("Recv complete from {} of size {} at time {}", src_id, message_size, recv_end_time);

    // msg_handler(fun_arg);
    return 0;
}

void ASTRASimGenieNetwork::sim_schedule_handler(void *func_arg) {
    SimScheduleArgs *args = static_cast<SimScheduleArgs*>(func_arg);
    if (!args) {
        throw std::runtime_error("null argument to sim_schedule_handler");
    }
    if (args->delta.time_res != AstraSim::time_type_e::NS) {
        throw std::runtime_error("Very unlikely: Time resolution for sim_schedule is not NS: " + std::to_string(args->delta.time_res));
    }

    #ifdef GENIE_CHROMETRACE_EVENT
    char event_name_buf[16];
    int len = snprintf(event_name_buf, sizeof(event_name_buf), "%d", args->event_id);
    static constexpr const char* SIM_SCHED_EVENT_STR = "SCHED_EVENT";
    EventType event_type = SCHEDULE_EVENT;
    int chrometrace_entry_idx = chrome_tracer->logEventStart(event_name_buf, SIM_SCHED_EVENT_STR, event_type);
    #endif

    auto current_time = std::chrono::steady_clock::now();
    auto duration = current_time - args->start_time;
    long double duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    if (args->delta.time_val > duration_ns) {
        // Still need to wait more
        Event event(SCHEDULE_EVENT, args);
        bool did_sleep = event_queue->add_poll_event(event);

        #ifdef GENIE_CHROMETRACE_EVENT
        // Only log every POLL_SKIP_MOD requests.
        if (!((_schedule_poll_counter % POLL_SKIP_MOD_INTERVAL))) {
            chrome_tracer->logEventEnd(chrometrace_entry_idx);
        } else {
            chrome_tracer->ignore_last_call();
        }
        _schedule_poll_counter += 1;
        #endif

    } else {
        // Event has completed. Trigger workload to move onto next step.
        args->callable->call(args->event, args->call_data);

        #ifdef GENIE_CHROMETRACE_EVENT
        chrome_tracer->logEventEnd(chrometrace_entry_idx, true);
        #endif
        delete args;
    }
    return;
}

void ASTRASimGenieNetwork::poll_send_handler(FuncArgs *fun_arg) {
    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* POLL_SEND_EVENT_NAME = "POLL_SEND";
    static constexpr const char* POLL_SEND_EVENT_STR = "POLL_SEND_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(POLL_SEND_EVENT_NAME, POLL_SEND_EVENT_STR, POLL_SEND);
    #endif
    PollSendArgs *args = static_cast<PollSendArgs*>(fun_arg);
    if (!args) {
        throw std::runtime_error("null argument to poll_send_handler");
    }

    int qp_idx = args->qp_idx; // Replacing 'stream_id' with QP idx
    int peer_rank = args->peer_rank;
    auto qp_manager = qp_managers[args->comm_group_id];
    auto sendComplete = qp_manager->send_buffers[peer_rank * qp_manager->nqps + qp_idx]->pollQP();
    if (genie_qpstats::enabled()) {
        auto& L = genie_qpstats::loop();
        L.poll_send++;
        if (sendComplete > 0) {
            L.poll_send_hit++;
            L.send_cqes += sendComplete;
            L.cqes_per_hit.add(sendComplete);
            const uint64_t t = genie_qpstats::now_ns();
            auto& st = qp_manager->stats[peer_rank * qp_manager->nqps + qp_idx];
            for (int c = 0; c < sendComplete && !st.s_q.empty(); c++) {
                st.s_lat.add(t - st.s_q.front());
                st.s_q.pop_front();
                st.s_inflight--;
                st.s_inflight_bytes -= st.s_qb.front();
                st.s_qb.pop_front();
                genie_qpstats::tracer().rec(genie_qpstats::T_SCQE, peer_rank, qp_idx, (uint32_t)st.s_msgs, t);
            }
            if (st.s_last_cqe) st.s_cqe_gap.add(t - st.s_last_cqe);
            st.s_last_cqe = t;
        }
    }
    // if (sendComplete > pending_send_cnt) {
    //     throw std::runtime_error(
    //         "pending_send_cnt underflow: pending=" +
    //         std::to_string(pending_send_cnt) +
    //         ", completions=" + std::to_string(sendComplete));
    // }
    // pending_send_cnt -= sendComplete;

    for (int cqe_idx = 0; cqe_idx < sendComplete; cqe_idx++) {
        AstraSim::sim_request snd_req;
        snd_req.srcRank = rank;
        snd_req.dstRank = peer_rank;
        snd_req.reqType = AstraSim::UINT8;
        snd_req.vnet = 0; // Irrelevant

        AstraSim::sim_request rcv_req;
        rcv_req.srcRank = peer_rank;
        rcv_req.reqType = AstraSim::UINT8;
        mark_complete(qp_idx, snd_req, rcv_req, true);
        // simple_ring_ptr->inject_next_msg_no_ehd(qp_idx, snd_req, rcv_req);
        // auto recv_args = (PollRecvArgs *)ring_buffer_recv_args[qp_idx]->dequeue();
        // recv_args->msg_handler(recv_args->fun_arg);
    }

    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx, sendComplete > 0);
    #endif

    Event event(POLL_SEND, fun_arg);
    event_queue->add_poll_event(event);
    return;
}

void ASTRASimGenieNetwork::sim_send_handler(FuncArgs *fun_arg) {
    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* SIM_SEND_EVENT_NAME = "SIM_SEND";
    static constexpr const char* SIM_SEND_EVENT_STR = "SIM_SEND_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(SIM_SEND_EVENT_NAME, SIM_SEND_EVENT_STR, SIM_SEND);
    #endif
    SimSendArgs *args = static_cast<SimSendArgs*>(fun_arg);
    if (!args) {
        throw std::runtime_error("null argument to sim_send_handler");
    }

    int peer_rank = args->peer_rank;
    // if (qp_manager->check_incoming_cts(peer_rank, args->qp_idx) == args->stream_id) {
    if (qp_managers[args->comm_group_id]->check_incoming_cts(peer_rank, args->qp_idx) >= 0) {
        // Using last 2 bits b/c we have 4 offsets RR.
        int buf_idx = args->stream_id & 3;

        if (genie_qpstats::enabled()) {
            auto* qpm = qp_managers[args->comm_group_id];
            auto& st = qpm->stats[peer_rank * qpm->nqps + args->qp_idx];
            const uint64_t t0 = genie_qpstats::now_ns();
            args->buf->send(buf_idx * MSG_SIZE_MB * 1024 * 1024, args->msg_size, buf_idx * MSG_SIZE_MB * 1024 * 1024, args->stream_id);
            const uint64_t t1 = genie_qpstats::now_ns();
            auto& L = genie_qpstats::loop();
            L.sim_send++;
            st.used = true;
            st.post_cost.add(t1 - t0);
            st.cts_wait.add(t0 - args->t_ready);
            if (args->spins == 0) st.s_cts_first++;
            st.s_cts_spins += args->spins;
            if (st.s_inflight == 0) {
                st.s_post_idle++;
                if (st.s_last_cqe) st.s_idle_ns += t0 - st.s_last_cqe;
            }
            st.s_msgs++;
            st.s_bytes += args->msg_size;
            st.s_inflight++;
            st.s_inflight_bytes += args->msg_size;
            st.s_q.push_back(t0);
            st.s_qb.push_back(args->msg_size);
            if (st.s_inflight > st.s_inflight_max) st.s_inflight_max = st.s_inflight;
            if (st.s_inflight_bytes > st.s_inflight_bytes_max) st.s_inflight_bytes_max = st.s_inflight_bytes;
            st.s_inflight_sum += st.s_inflight;
            st.s_inflight_bytes_sum += st.s_inflight_bytes;
            genie_qpstats::tracer().rec(genie_qpstats::T_POST, peer_rank, args->qp_idx, args->stream_id, t0);
        } else {
            args->buf->send(buf_idx * MSG_SIZE_MB * 1024 * 1024, args->msg_size, buf_idx * MSG_SIZE_MB * 1024 * 1024, args->stream_id);
        }
        // pending_send_cnt++;
        sim_send_args->return_finished_slot(args);
    } else {
        // Re-enqueue the send handler to poll again later.
        if (genie_qpstats::enabled()) {
            args->spins++;
            genie_qpstats::loop().sim_send_miss++;
        }
        // std::cout << "CTS not registered for peer " << peer_rank << " and QP " << args->qp_idx << " for stream_id " << args->stream_id << ". Re-enqueueing send handler." << std::endl;
        Event event(SIM_SEND, fun_arg);
        event_queue->add_event(event);
    }

    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx);
    #endif
    return;
}

void ASTRASimGenieNetwork::poll_recv_handler(FuncArgs *fun_args) {

    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* POLL_RECV_EVENT_NAME = "POLL_RECV";
    static constexpr const char* POLL_RECV_EVENT_STR = "POLL_RECV_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(POLL_RECV_EVENT_NAME, POLL_RECV_EVENT_STR, POLL_RECV);
    #endif
    auto args = static_cast<PollRecvArgs*>(fun_args);


    auto recvComplete = args->buf->pollQP();
    if (genie_qpstats::enabled()) {
        auto& L = genie_qpstats::loop();
        L.poll_recv++;
        if (recvComplete > 0) {
            L.poll_recv_hit++;
            L.recv_cqes += recvComplete;
            L.cqes_per_hit.add(recvComplete);
            const uint64_t t = genie_qpstats::now_ns();
            auto* qpm = qp_managers[args->comm_group_id];
            auto& st = qpm->stats[args->peer_rank * qpm->nqps + args->qp_idx];
            for (int c = 0; c < recvComplete && !st.r_q.empty(); c++) {
                st.r_lat.add(t - st.r_q.front());
                st.r_q.pop_front();
                st.r_inflight--;
                genie_qpstats::tracer().rec(genie_qpstats::T_RCQE, args->peer_rank, args->qp_idx, (uint32_t)st.r_msgs, t);
            }
            if (st.r_last_cqe) st.r_cqe_gap.add(t - st.r_last_cqe);
            st.r_last_cqe = t;
        }
    }
    // if (recvComplete > pending_recv_cnt) {
    //     throw std::runtime_error(
    //         "pending_recv_cnt underflow: pending=" +
    //         std::to_string(pending_recv_cnt) +
    //         ", completions=" + std::to_string(recvComplete));
    // }
    // pending_recv_cnt -= recvComplete;
    int qp_idx = args->qp_idx; // Get qp_idx directly from args. SimpleRing makes it impossible to infer qp_idx from stream_id.
    int peer_rank = args->peer_rank;

    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx, recvComplete > 0);
    #endif

    for (int cqe_idx = 0; cqe_idx < recvComplete; cqe_idx++) {
        AstraSim::sim_request snd_req;
        snd_req.srcRank = rank;
        if (IS_PINGPONG) {
            snd_req.dstRank = rank ^ 1;
        } else {
            snd_req.dstRank = peer_rank;
        }
        snd_req.reqType = AstraSim::UINT8;
        snd_req.vnet = 0; // Irrelevant

        AstraSim::sim_request rcv_req;
        if (IS_PINGPONG) {
            rcv_req.srcRank = rank ^ 1;
        } else {
            rcv_req.srcRank = peer_rank;
        }
        rcv_req.reqType = AstraSim::UINT8;
        mark_complete(qp_idx, snd_req, rcv_req, false);
        // simple_ring_ptr->inject_next_msg_no_ehd(qp_idx, snd_req, rcv_req);
        // auto recv_args = (PollRecvArgs *)ring_buffer_recv_args[qp_idx]->dequeue();
        // recv_args->msg_handler(recv_args->fun_arg);
    }

    
    Event event(POLL_RECV, fun_args);
    event_queue->add_poll_event(event);
    return;
}

void ASTRASimGenieNetwork::sim_recv_handler(FuncArgs *fun_args) {
    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* SIM_RECV_EVENT_NAME = "SIM_RECV";
    static constexpr const char* SIM_RECV_EVENT_STR = "SIM_RECV_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(SIM_RECV_EVENT_NAME, SIM_RECV_EVENT_STR, SIM_RECV);
    #endif

    auto args = static_cast<SimRecvArgs*>(fun_args);
    if (!args) {
        throw std::runtime_error("null argument to sim_recv_handler");
    }

    // Assumption: The send should have long completed by now.
    int qp_idx = args->qp_idx; // Get qp_idx directly from args. SimpleRing makes it impossible to infer qp_idx from stream_id.
    int peer_rank = args->peer_rank;
    auto qp_manager = qp_managers[args->comm_group_id];
    qp_manager->poll_send_cts_complete(peer_rank, qp_idx);
    int buf_idx = args->stream_id & 3; // Using last 2 bits b/c we have 4 offsets RR.
    args->buf->recv(args->stream_id, buf_idx * MSG_SIZE_MB * 1024 * 1024, MSG_SIZE_MB * 1024 * 1024);
    // pending_recv_cnt++;

    qp_manager->send_cts_message(peer_rank, qp_idx, args->stream_id);
    if (genie_qpstats::enabled()) {
        auto& st = qp_manager->stats[peer_rank * qp_manager->nqps + qp_idx];
        const uint64_t t = genie_qpstats::now_ns();
        genie_qpstats::loop().sim_recv++;
        st.used = true;
        st.r_msgs++;
        st.r_inflight++;
        if (st.r_inflight > st.r_inflight_max) st.r_inflight_max = st.r_inflight;
        st.r_q.push_back(t);
        genie_qpstats::tracer().rec(genie_qpstats::T_RPOST, peer_rank, qp_idx, args->stream_id, t);
    }

    // PollRecvArgs *event_args = new PollRecvArgs{
    //     args->stream_id,
    //     args->qp_idx,
    //     args->buf,
    //     args->msg_handler,
    //     args->fun_arg
    // };

    // ring_buffer_recv_args[qp_idx]->enqueue(event_args);
    sim_recv_args->return_finished_slot(args);

    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx);
    #endif
    return;
}

void ASTRASimGenieNetwork::mark_complete(int qp_idx, AstraSim::sim_request& snd_req, AstraSim::sim_request& rcv_req, bool is_send) {
    if (genie_collective_ptr) {
        if (is_send) {
            genie_collective_ptr->mark_send_complete(qp_idx, snd_req, rcv_req);
        } else {
            genie_collective_ptr->mark_recv_complete(qp_idx, snd_req, rcv_req);
        }
    } else {
        throw std::runtime_error(
            "Error: genie_collective_ptr is null in mark_complete, qp_idx=" + std::to_string(qp_idx) +
            ", is_send=" + std::to_string(static_cast<int>(is_send)));
    }
// }
//
// void ASTRASimGenieNetwork::assert_no_pending_operations(int iteration) const {
//     // _logger->info(
//     //     "Iteration {} boundary: pending_send_cnt={}, pending_recv_cnt={}",
//     //     iteration, pending_send_cnt, pending_recv_cnt);
//     if (pending_send_cnt != 0 || pending_recv_cnt != 0) {
//         throw std::runtime_error(
//             "Iteration " + std::to_string(iteration) +
//             " completed with pending operations: pending_send_cnt=" +
//             std::to_string(pending_send_cnt) +
//             ", pending_recv_cnt=" + std::to_string(pending_recv_cnt));
//     }
}