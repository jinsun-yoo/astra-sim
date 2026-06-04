/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/SimpleTree.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"
#include <cstring>
#include <stdexcept>

using namespace AstraSim;

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
int SimpleTree::collective_size_from_env_mb = -1;
int SimpleTree::num_qps = -1;

void SimpleTree::get_collective_size_from_env() {
    if (collective_size_from_env_mb == -1) {
        const char* env_str = getenv("GENIE_SIMPLETREE_COLLECTIVE_SIZE_MB");
        collective_size_from_env_mb =
            (env_str && *env_str != '\0') ? std::stoi(env_str) : 0;
        std::cout << "SimpleTree: collective_size_from_env_mb="
                  << collective_size_from_env_mb
                  << " (GENIE_SIMPLETREE_COLLECTIVE_SIZE_MB="
                  << (env_str ? env_str : "null") << ")" << std::endl;
    }
}

void SimpleTree::get_num_qps_from_env() {
    if (num_qps == -1) {
        const char* env_str = getenv("GENIE_NUM_QPS");
        num_qps = (env_str && *env_str != '\0') ? std::stoi(env_str) : 2;
        if (num_qps > TREE_MAX_NUM_QPS) {
            throw std::runtime_error(
                "SimpleTree: GENIE_NUM_QPS=" + std::to_string(num_qps) +
                " exceeds TREE_MAX_NUM_QPS=" + std::to_string(TREE_MAX_NUM_QPS));
        }
        std::cout << "SimpleTree: num_qps=" << num_qps
                  << " (GENIE_NUM_QPS="
                  << (env_str ? env_str : "unset, default 2") << ")"
                  << std::endl;
    }
}

// ---------------------------------------------------------------------------
// TRACE_SIMPLETREE helper: print per-rank state for every completion.
// ---------------------------------------------------------------------------
#ifdef TRACE_SIMPLETREE
static void trace_print_state(int rank,
                               const char* label,
                               int peer_rank,
                               int qp_idx,
                               int polled,
                               int total,
                               int num_qps,
                               int (*polled_send)[TREE_MAX_NUM_QPS],
                               int (*polled_recv)[TREE_MAX_NUM_QPS],
                               int num_parent_peers,
                               int num_child_peers) {
    std::cout << "[TRACE_SIMPLETREE rank=" << rank << "] " << label
              << " peer=" << peer_rank
              << " qp=" << qp_idx
              << " polled=" << polled << "/" << total << std::endl;

    std::cout << "[TRACE_SIMPLETREE rank=" << rank << "]  send_cnts:";
    for (int p = 0; p < num_parent_peers; p++)
        for (int q = 0; q < num_qps; q++)
            std::cout << " [p" << p << ",q" << q << "]=" << polled_send[p][q];
    std::cout << std::endl;

    std::cout << "[TRACE_SIMPLETREE rank=" << rank << "]  recv_cnts:";
    for (int p = 0; p < num_child_peers; p++)
        for (int q = 0; q < num_qps; q++)
            std::cout << " [p" << p << ",q" << q << "]=" << polled_recv[p][q];
    std::cout << std::endl;
}
#endif  // TRACE_SIMPLETREE

// ---------------------------------------------------------------------------
// Constructor
//
// Hardcoded double binary tree topology for 4 ranks:
//
//   Tree A  Reduce  : 0->1, 2->1, 1->3     Broadcast: 3->1, 1->0, 1->2
//   Tree B  Reduce  : 1->2, 3->2, 2->0     Broadcast: 0->2, 2->1, 2->3
//
//   Per-rank roles and peer sets:
//   Rank 0 : Tree-A Leaf  / Tree-B Root
//     parent_peers=[1],    child_peers=[2]
//     parent_send_needs_children=[false]
//     reduce_parent_conditional_idx=-1, bcast_send_immediate=true, bcast_trigger_parent_idx=-1
//
//   Rank 1 : Tree-A Intermediate / Tree-B Leaf
//     parent_peers=[3,2],  child_peers=[0,2]
//     parent_send_needs_children=[true,false]   (send->3 gated on children; send->2 immediate)
//     reduce_parent_conditional_idx=0, bcast_send_immediate=false, bcast_trigger_parent_idx=0
//
//   Rank 2 : Tree-A Leaf  / Tree-B Intermediate
//     parent_peers=[1,0],  child_peers=[1,3]
//     parent_send_needs_children=[false,true]   (send->1 immediate; send->0 gated on children)
//     reduce_parent_conditional_idx=1, bcast_send_immediate=false, bcast_trigger_parent_idx=1
//
//   Rank 3 : Tree-A Root  / Tree-B Leaf
//     parent_peers=[2],    child_peers=[1]
//     parent_send_needs_children=[false]
//     reduce_parent_conditional_idx=-1, bcast_send_immediate=true, bcast_trigger_parent_idx=-1
// ---------------------------------------------------------------------------
SimpleTree::SimpleTree(int id,
                       uint64_t data_size_bytes,
                       ComType collective_type,
                       int comm_group_id)
    : GenieCollective() {
    if (id < 0 || id >= TREE_NUM_RANKS) {
        throw std::runtime_error(
            "SimpleTree: rank " + std::to_string(id) +
            " is out of range [0, " + std::to_string(TREE_NUM_RANKS) + ")");
    }

    this->id = id;
    this->comm_group_id = comm_group_id;
    this->collective_type = collective_type;
    this->current_phase = Phase::REDUCE;

    // ----------------------------------------------------------------
    // Build topology lookup tables. All arrays default to -1.
    // ----------------------------------------------------------------
    std::fill(parent_rank_to_idx, parent_rank_to_idx + TREE_NUM_RANKS, -1);
    std::fill(child_rank_to_idx,  child_rank_to_idx  + TREE_NUM_RANKS, -1);

    // Topology tables indexed by rank id:
    //   {parent_peers...}, num_parent_peers,
    //   {child_peers...},  num_child_peers,
    //   {parent_send_needs_children...},
    //   reduce_parent_conditional_idx,
    //   bcast_send_immediate,
    //   bcast_trigger_parent_idx

    // Rank 0: Tree-A Leaf (parent=1), Tree-B Root (child=2)
    if (id == 0) {
        num_parent_peers = 1;
        parent_peers[0] = 1;
        parent_send_needs_children[0] = false;
        reduce_parent_conditional_idx = -1;

        num_child_peers = 1;
        child_peers[0] = 2;

        bcast_send_immediate      = true;
        bcast_trigger_parent_idx  = -1;

    // Rank 1: Tree-A Intermediate (children=0,2; parent=3), Tree-B Leaf (parent=2)
    } else if (id == 1) {
        num_parent_peers = 2;
        parent_peers[0] = 3;  parent_send_needs_children[0] = true;   // conditional
        parent_peers[1] = 2;  parent_send_needs_children[1] = false;  // immediate (Tree-B leaf)
        reduce_parent_conditional_idx = 0;

        num_child_peers = 2;
        child_peers[0] = 0;
        child_peers[1] = 2;

        bcast_send_immediate      = false;
        bcast_trigger_parent_idx  = 0;  // recv from parent_peers[0]=3 triggers child sends

    // Rank 2: Tree-A Leaf (parent=1), Tree-B Intermediate (children=1,3; parent=0)
    } else if (id == 2) {
        num_parent_peers = 2;
        parent_peers[0] = 1;  parent_send_needs_children[0] = false;  // immediate (Tree-A leaf)
        parent_peers[1] = 0;  parent_send_needs_children[1] = true;   // conditional
        reduce_parent_conditional_idx = 1;

        num_child_peers = 2;
        child_peers[0] = 1;
        child_peers[1] = 3;

        bcast_send_immediate      = false;
        bcast_trigger_parent_idx  = 1;  // recv from parent_peers[1]=0 triggers child sends

    // Rank 3: Tree-A Root (child=1), Tree-B Leaf (parent=2)
    } else {  // id == 3
        num_parent_peers = 1;
        parent_peers[0] = 2;
        parent_send_needs_children[0] = false;
        reduce_parent_conditional_idx = -1;

        num_child_peers = 1;
        child_peers[0] = 1;

        bcast_send_immediate      = true;
        bcast_trigger_parent_idx  = -1;
    }

    // Build reverse-lookup maps.
    for (int i = 0; i < num_parent_peers; i++)
        parent_rank_to_idx[parent_peers[i]] = i;
    for (int i = 0; i < num_child_peers; i++)
        child_rank_to_idx[child_peers[i]] = i;

    // ----------------------------------------------------------------
    // Message count: data is split evenly across the two trees.
    // num_msgs_per_qp = (collective_size_mb / 2) / (num_qps * TREE_MSG_SIZE_MB)
    // ----------------------------------------------------------------
    get_collective_size_from_env();
    get_num_qps_from_env();

    int data_size_mb = static_cast<int>(data_size_bytes / (1024 * 1024));
    this->collective_size_mb =
        collective_size_from_env_mb ? collective_size_from_env_mb : data_size_mb;
    this->num_msgs_per_qp =
        this->collective_size_mb / (2 * num_qps * TREE_MSG_SIZE_MB);

    if (this->num_msgs_per_qp <= 0) {
        throw std::runtime_error(
            "SimpleTree: num_msgs_per_qp=" +
            std::to_string(this->num_msgs_per_qp) +
            " is non-positive (collective_size_mb=" +
            std::to_string(this->collective_size_mb) + ")");
    }

    // ----------------------------------------------------------------
    // Initialise counters and marker.
    // ----------------------------------------------------------------
    std::memset(sim_send_cnt,    0, sizeof(sim_send_cnt));
    std::memset(sim_recv_cnt,    0, sizeof(sim_recv_cnt));
    std::memset(polled_send_cnt, 0, sizeof(polled_send_cnt));
    std::memset(polled_recv_cnt, 0, sizeof(polled_recv_cnt));

    if (reduce_parent_conditional_idx >= 0) {
        child_reduce_marker.assign(
            num_qps, std::vector<int>(this->num_msgs_per_qp, 0));
    }

    std::memset(cond_send_inflight, 0, sizeof(cond_send_inflight));
    std::memset(cond_send_pending,  0, sizeof(cond_send_pending));

    // Algorithm base-class fields.
    this->data_size       = data_size_bytes;
    this->final_data_size = data_size_bytes;
    this->comType         = ComType::All_Reduce;

    std::cout << "SimpleTree rank=" << id
              << " collective_size_mb=" << collective_size_mb
              << " num_msgs_per_qp=" << num_msgs_per_qp
              << " num_qps=" << num_qps
              << std::endl;

#ifdef TRACE_SIMPLETREE
    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] constructed"
              << " parent_peers=[";
    for (int i = 0; i < num_parent_peers; i++)
        std::cout << (i ? "," : "") << parent_peers[i]
                  << "(cond=" << parent_send_needs_children[i] << ")";
    std::cout << "] child_peers=[";
    for (int i = 0; i < num_child_peers; i++)
        std::cout << (i ? "," : "") << child_peers[i];
    std::cout << "] reduce_conditional_idx=" << reduce_parent_conditional_idx
              << " bcast_immediate=" << bcast_send_immediate
              << " bcast_trigger_parent_idx=" << bcast_trigger_parent_idx
              << std::endl;
#endif
}

// ---------------------------------------------------------------------------
// run() — entry point from the stream scheduler.
// ---------------------------------------------------------------------------
void SimpleTree::run(EventType event, CallData* data) {
    if (event == EventType::StreamInit) {
        sim_request snd_req;
        snd_req.srcRank = id;
        snd_req.reqType = UINT8;
        snd_req.vnet    = 0;
        sim_request rcv_req;
        rcv_req.reqType = UINT8;
        rcv_req.vnet    = 0;
        inject_init_msgs(snd_req, rcv_req);
    } else if (event == EventType::PacketReceived) {
        throw std::runtime_error(
            "SimpleTree: PacketReceived must be handled via mark_recv_complete "
            "(genie path), not run().");
    }
}

// ---------------------------------------------------------------------------
// inject_init_msgs — reduce phase kick-off.
// ---------------------------------------------------------------------------
void SimpleTree::inject_init_msgs(sim_request& /*snd_req*/,
                                  sim_request& /*rcv_req*/) {
    start_ts_nano = stream->owner->comm_NI->sim_get_time().time_val;

#ifdef TRACE_SIMPLETREE
    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] inject_init_msgs"
              << " ts=" << start_ts_nano
              << " num_child_peers=" << num_child_peers
              << " num_parent_peers=" << num_parent_peers
              << std::endl;
#endif

    int init_cnt = std::min(num_msgs_per_qp, TREE_NUM_INFLIGHT_CHUNKS_PER_QP);

    // Post initial recvs from every child peer (reduce phase).
    for (int ci = 0; ci < num_child_peers; ci++) {
        for (int i = 0; i < init_cnt; i++) {
            for (int q = 0; q < num_qps; q++) {
                post_recv(child_peers[ci], ci, q);
            }
        }
    }

    // Post initial sends to immediate parent peers (leaf roles, no gating).
    for (int pi = 0; pi < num_parent_peers; pi++) {
        if (!parent_send_needs_children[pi]) {
            for (int i = 0; i < init_cnt; i++) {
                for (int q = 0; q < num_qps; q++) {
                    post_send(parent_peers[pi], pi, q);
                }
            }
        }
    }
    // Conditional parent sends (intermediate roles) are posted later from
    // mark_recv_complete once all child recvs for a message arrive.
}

// ---------------------------------------------------------------------------
// post_send / post_recv — low-level single-message injection.
//
// peer_idx is the index into the currently-active send/recv list:
//   REDUCE:    send list = parent_peers,  recv list = child_peers
//   BROADCAST: send list = child_peers,   recv list = parent_peers
// ---------------------------------------------------------------------------
void SimpleTree::post_send(int peer_rank, int peer_idx, int qp_idx) {
    if (sim_send_cnt[peer_idx][qp_idx] >= num_msgs_per_qp) return;

    sim_request snd_req;
    snd_req.srcRank = id;
    snd_req.dstRank = peer_rank;
    snd_req.reqType = UINT8;
    snd_req.vnet    = 0;
    snd_req.tag     = sim_send_cnt[peer_idx][qp_idx];

    stream->owner->front_end_sim_send(
        0, Sys::dummy_data, TREE_MSG_SIZE_MB * 1024 * 1024, UINT8, peer_rank,
        qp_idx, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        comm_group_id, &Sys::handleEvent, nullptr);

    sim_send_cnt[peer_idx][qp_idx]++;
}

void SimpleTree::post_recv(int peer_rank, int peer_idx, int qp_idx) {
    if (sim_recv_cnt[peer_idx][qp_idx] >= num_msgs_per_qp) return;

    sim_request rcv_req;
    rcv_req.srcRank = peer_rank;
    rcv_req.reqType = UINT8;
    rcv_req.vnet    = 0;
    rcv_req.tag     = sim_recv_cnt[peer_idx][qp_idx];

    RecvPacketEventHandlerData* ehd = new RecvPacketEventHandlerData(
        stream, stream->owner->id, EventType::PacketReceived,
        qp_idx, sim_recv_cnt[peer_idx][qp_idx]);

    stream->owner->front_end_sim_recv(
        0, Sys::dummy_data, TREE_MSG_SIZE_MB * 1024 * 1024, UINT8, peer_rank,
        qp_idx, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        comm_group_id, &Sys::handleEvent, ehd);

    sim_recv_cnt[peer_idx][qp_idx]++;
}

// ---------------------------------------------------------------------------
// mark_recv_complete — called by genie network on every recv completion.
//
// snd_req.dstRank (set by the genie network layer) identifies the remote peer
// for both send and recv completions.
// ---------------------------------------------------------------------------
void SimpleTree::mark_recv_complete(int qp_idx,
                                    sim_request& snd_req,
                                    sim_request& /*rcv_req*/) {
    // The genie network layer sets snd_req.dstRank = peer_rank for BOTH send
    // and recv completions (see genie_network.cc sim_send/recv_poll_handler).
    // For a recv completion, dstRank is therefore the rank we received FROM.
    int peer_rank = snd_req.dstRank;

    if (current_phase == Phase::REDUCE) {
        // During reduce we recv from child peers.
        int peer_idx = child_rank_to_idx[peer_rank];
        if (peer_idx < 0) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " mark_recv_complete(REDUCE): unexpected peer_rank=" +
                std::to_string(peer_rank));
        }

        int msg_idx = polled_recv_cnt[peer_idx][qp_idx];
        if (msg_idx >= num_msgs_per_qp) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " reduce recv spurious: peer=" + std::to_string(peer_rank) +
                " qp=" + std::to_string(qp_idx));
        }
        polled_recv_cnt[peer_idx][qp_idx]++;

#ifdef TRACE_SIMPLETREE
        std::cout << "[TRACE_SIMPLETREE rank=" << id << "] REDUCE recv_complete"
                  << " from=" << peer_rank
                  << " child_idx=" << peer_idx
                  << " qp=" << qp_idx
                  << " msg=" << msg_idx << "/" << num_msgs_per_qp
                  << std::endl;
#endif

        // Sliding window: post next recv from this child.
        post_recv(child_peers[peer_idx], peer_idx, qp_idx);

        // Gate conditional parent send: when all children deliver msg_idx,
        // post the conditional parent send for that slot using a sliding
        // window (cond_send_inflight) to avoid RingTrain overflow.
        if (reduce_parent_conditional_idx >= 0) {
            child_reduce_marker[qp_idx][msg_idx]++;
#ifdef TRACE_SIMPLETREE
            std::cout << "[TRACE_SIMPLETREE rank=" << id << "] REDUCE marker"
                      << " qp=" << qp_idx
                      << " msg=" << msg_idx
                      << " marker=" << child_reduce_marker[qp_idx][msg_idx]
                      << "/" << num_child_peers
                      << std::endl;
#endif
            if (child_reduce_marker[qp_idx][msg_idx] == num_child_peers) {
                if (cond_send_inflight[qp_idx] < TREE_NUM_INFLIGHT_CHUNKS_PER_QP) {
                    int cpi = reduce_parent_conditional_idx;
#ifdef TRACE_SIMPLETREE
                    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] REDUCE cond_send"
                              << " to=" << parent_peers[cpi]
                              << " qp=" << qp_idx
                              << " msg=" << msg_idx
                              << " inflight=" << cond_send_inflight[qp_idx]+1
                              << std::endl;
#endif
                    post_send(parent_peers[cpi], cpi, qp_idx);
                    cond_send_inflight[qp_idx]++;
                } else {
                    cond_send_pending[qp_idx]++;
#ifdef TRACE_SIMPLETREE
                    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] REDUCE cond_send_queued"
                              << " qp=" << qp_idx
                              << " msg=" << msg_idx
                              << " pending=" << cond_send_pending[qp_idx]
                              << std::endl;
#endif
                }
            }
        }

        check_reduce_complete();

    } else {  // Phase::BROADCAST
        // During broadcast we recv from parent peers.
        int peer_idx = parent_rank_to_idx[peer_rank];
        if (peer_idx < 0) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " mark_recv_complete(BROADCAST): unexpected peer_rank=" +
                std::to_string(peer_rank));
        }

        if (polled_recv_cnt[peer_idx][qp_idx] >= num_msgs_per_qp) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " bcast recv spurious: peer=" + std::to_string(peer_rank) +
                " qp=" + std::to_string(qp_idx));
        }
        polled_recv_cnt[peer_idx][qp_idx]++;

#ifdef TRACE_SIMPLETREE
        std::cout << "[TRACE_SIMPLETREE rank=" << id << "] BCAST recv_complete"
                  << " from=" << peer_rank
                  << " parent_idx=" << peer_idx
                  << " qp=" << qp_idx
                  << " polled=" << polled_recv_cnt[peer_idx][qp_idx] << "/" << num_msgs_per_qp
                  << std::endl;
#endif

        // Sliding window: post next recv from this parent.
        post_recv(parent_peers[peer_idx], peer_idx, qp_idx);

        // If this parent's recv completion triggers child sends, post them
        // using a per-child sliding window to avoid RingTrain overflow.
        if (peer_idx == bcast_trigger_parent_idx) {
#ifdef TRACE_SIMPLETREE
            std::cout << "[TRACE_SIMPLETREE rank=" << id << "] BCAST trigger_send"
                      << " qp=" << qp_idx
                      << " num_children=" << num_child_peers
                      << std::endl;
#endif
            for (int ci = 0; ci < num_child_peers; ci++) {
                if (bcast_trig_inflight[ci][qp_idx] < TREE_NUM_INFLIGHT_CHUNKS_PER_QP) {
                    post_send(child_peers[ci], ci, qp_idx);
                    bcast_trig_inflight[ci][qp_idx]++;
                } else {
                    bcast_trig_pending[ci][qp_idx]++;
                }
            }
        }

        check_bcast_complete();
    }
}

// ---------------------------------------------------------------------------
// mark_send_complete — called by genie network on every send completion.
// ---------------------------------------------------------------------------
void SimpleTree::mark_send_complete(int qp_idx,
                                    sim_request& snd_req,
                                    sim_request& /*rcv_req*/) {
    int peer_rank = snd_req.dstRank;

    if (current_phase == Phase::REDUCE) {
        // During reduce we send to parent peers.
        int peer_idx = parent_rank_to_idx[peer_rank];
        if (peer_idx < 0) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " mark_send_complete(REDUCE): unexpected peer_rank=" +
                std::to_string(peer_rank));
        }

        if (polled_send_cnt[peer_idx][qp_idx] >= num_msgs_per_qp) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " reduce send spurious: peer=" + std::to_string(peer_rank) +
                " qp=" + std::to_string(qp_idx));
        }
        polled_send_cnt[peer_idx][qp_idx]++;

#ifdef TRACE_SIMPLETREE
        std::cout << "[TRACE_SIMPLETREE rank=" << id << "] REDUCE send_complete"
                  << " to=" << peer_rank
                  << " parent_idx=" << peer_idx
                  << " qp=" << qp_idx
                  << " polled=" << polled_send_cnt[peer_idx][qp_idx] << "/" << num_msgs_per_qp
                  << std::endl;
#endif

        // Sliding window for immediate parent sends (leaf roles).
        // Conditional parent sends drain from the pending queue here.
        if (!parent_send_needs_children[peer_idx]) {
            post_send(parent_peers[peer_idx], peer_idx, qp_idx);
        } else {
            // Conditional send completed: free one inflight slot and inject
            // the next pending conditional send (if any).
            cond_send_inflight[qp_idx]--;
            if (cond_send_pending[qp_idx] > 0) {
                cond_send_pending[qp_idx]--;
                post_send(parent_peers[peer_idx], peer_idx, qp_idx);
                cond_send_inflight[qp_idx]++;
            }
        }

        check_reduce_complete();

    } else {  // Phase::BROADCAST
        // During broadcast we send to child peers.
        int peer_idx = child_rank_to_idx[peer_rank];
        if (peer_idx < 0) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " mark_send_complete(BROADCAST): unexpected peer_rank=" +
                std::to_string(peer_rank));
        }

        if (polled_send_cnt[peer_idx][qp_idx] >= num_msgs_per_qp) {
            throw std::runtime_error(
                "SimpleTree rank=" + std::to_string(id) +
                " bcast send spurious: peer=" + std::to_string(peer_rank) +
                " qp=" + std::to_string(qp_idx));
        }
        polled_send_cnt[peer_idx][qp_idx]++;

#ifdef TRACE_SIMPLETREE
        std::cout << "[TRACE_SIMPLETREE rank=" << id << "] BCAST send_complete"
                  << " to=" << peer_rank
                  << " child_idx=" << peer_idx
                  << " qp=" << qp_idx
                  << " polled=" << polled_send_cnt[peer_idx][qp_idx] << "/" << num_msgs_per_qp
                  << std::endl;
#endif

        // Sliding window for immediate child sends (root ranks in bcast).
        // Triggered child sends drain from the pending queue here (non-immediate).
        if (bcast_send_immediate) {
            post_send(child_peers[peer_idx], peer_idx, qp_idx);
        } else {
            bcast_trig_inflight[peer_idx][qp_idx]--;
            if (bcast_trig_pending[peer_idx][qp_idx] > 0) {
                bcast_trig_pending[peer_idx][qp_idx]--;
                post_send(child_peers[peer_idx], peer_idx, qp_idx);
                bcast_trig_inflight[peer_idx][qp_idx]++;
            }
        }

        check_bcast_complete();
    }
}

// ---------------------------------------------------------------------------
// check_reduce_complete — called after every completion during REDUCE phase.
// ---------------------------------------------------------------------------
void SimpleTree::check_reduce_complete() {
    // Guard against re-entry (e.g. if multiple completions arrive simultaneously).
    if (current_phase != Phase::REDUCE) return;

    for (int i = 0; i < num_parent_peers; i++)
        for (int q = 0; q < num_qps; q++)
            if (polled_send_cnt[i][q] < num_msgs_per_qp) return;

    for (int i = 0; i < num_child_peers; i++)
        for (int q = 0; q < num_qps; q++)
            if (polled_recv_cnt[i][q] < num_msgs_per_qp) return;

#ifdef TRACE_SIMPLETREE
    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] REDUCE phase complete -> start_broadcast" << std::endl;
#endif
    start_broadcast();
}

// ---------------------------------------------------------------------------
// check_bcast_complete — called after every completion during BROADCAST phase.
// ---------------------------------------------------------------------------
void SimpleTree::check_bcast_complete() {
    if (current_phase != Phase::BROADCAST) return;

    for (int i = 0; i < num_child_peers; i++)
        for (int q = 0; q < num_qps; q++)
            if (polled_send_cnt[i][q] < num_msgs_per_qp) return;

    for (int i = 0; i < num_parent_peers; i++)
        for (int q = 0; q < num_qps; q++)
            if (polled_recv_cnt[i][q] < num_msgs_per_qp) return;

#ifdef TRACE_SIMPLETREE
    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] BCAST phase complete -> exit" << std::endl;
#endif
    exit();
}

// ---------------------------------------------------------------------------
// start_broadcast — transitions from REDUCE to BROADCAST phase.
// ---------------------------------------------------------------------------
void SimpleTree::start_broadcast() {
    // Reset all per-phase counters; counter semantics flip (see header).
    std::memset(sim_send_cnt,      0, sizeof(sim_send_cnt));
    std::memset(sim_recv_cnt,      0, sizeof(sim_recv_cnt));
    std::memset(polled_send_cnt,   0, sizeof(polled_send_cnt));
    std::memset(polled_recv_cnt,   0, sizeof(polled_recv_cnt));
    std::memset(bcast_trig_inflight, 0, sizeof(bcast_trig_inflight));
    std::memset(bcast_trig_pending,  0, sizeof(bcast_trig_pending));

    current_phase = Phase::BROADCAST;

    int init_cnt = std::min(num_msgs_per_qp, TREE_NUM_INFLIGHT_CHUNKS_PER_QP);

#ifdef TRACE_SIMPLETREE
    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] start_broadcast"
              << " init_cnt=" << init_cnt
              << " bcast_send_immediate=" << bcast_send_immediate
              << std::endl;
#endif

    // Post initial recvs from every parent peer (everyone recvs in bcast).
    for (int pi = 0; pi < num_parent_peers; pi++) {
        for (int i = 0; i < init_cnt; i++) {
            for (int q = 0; q < num_qps; q++) {
                post_recv(parent_peers[pi], pi, q);
            }
        }
    }

    // Root ranks: post initial sends to all child peers immediately.
    if (bcast_send_immediate) {
        for (int ci = 0; ci < num_child_peers; ci++) {
            for (int i = 0; i < init_cnt; i++) {
                for (int q = 0; q < num_qps; q++) {
                    post_send(child_peers[ci], ci, q);
                }
            }
        }
    }
    // Intermediate ranks: child sends will be triggered by parent recv
    // completions in mark_recv_complete (bcast_trigger_parent_idx).
}

// ---------------------------------------------------------------------------
// record_stats / exit
// ---------------------------------------------------------------------------
void SimpleTree::record_stats() {
    Tick end_ts_nano = stream->owner->comm_NI->sim_get_time().time_val;
    int elapsed_ns   = static_cast<int>(end_ts_nano - start_ts_nano);
    // Reuse record_ring_coll as a proxy; no tree-specific stat method exists yet.
    stream->owner->stat_counter->record_ring_coll(
        elapsed_ns, collective_size_mb, num_qps, num_msgs_per_qp,
        collective_type);
}

void SimpleTree::exit() {
#ifdef TRACE_SIMPLETREE
    std::cout << "[TRACE_SIMPLETREE rank=" << id << "] exit: collective done" << std::endl;
#endif
    record_stats();
    stream->owner->unload_genie_collective();
    stream->owner->proceed_to_next_vnet_baseline((StreamBaseline*)stream);
}
