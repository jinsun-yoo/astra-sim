#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/SimpleSendrecv.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"
#include <unistd.h>

using namespace AstraSim;

// Static member variable definition
int SimpleSendrecv::collective_size_from_env_mb = -1;

void SimpleSendrecv::get_collective_size_from_env() {
    if (collective_size_from_env_mb == -1) {
        const char* env_str = getenv("GENIE_SIMPLERA2A_COLLECTIVE_SIZE_MB");
        collective_size_from_env_mb = (env_str && *env_str != '\0') ? std::stoi(env_str) : 0;
        std::cout << "SimpleSendrecv: collective_size_from_env_mb is set to " << collective_size_from_env_mb << " MB based on environment variable GENIE_SIMPLERA2A_COLLECTIVE_SIZE_MB=" << (env_str ? env_str : "null") << std::endl;
    }
    return;
}

SimpleSendrecv::SimpleSendrecv(int id, int peer_rank, bool is_send, uint64_t data_size_bytes, Sys *sys, WorkloadLayerHandlerData* wlhd, int comm_group_id): GenieCollective() {
    this->id = id;
    this->peer_rank = peer_rank;
    this->is_send = is_send;
    this->comm_group_id = comm_group_id;
    this->sys = sys;
    this->wlhd = wlhd;
    get_collective_size_from_env();
    int data_size_mb = data_size_bytes / (1024 * 1024);
    this->collective_size_mb = collective_size_from_env_mb ? collective_size_from_env_mb : data_size_mb;
    int num_total_steps = this->collective_size_mb * (1024 * 1024 / 131072);
    this->num_msgs_per_qp = num_total_steps / (A2A_NUM_QPS_PER_RANK);
    // This macro is defined in the top CMakeLists.txt
    #ifdef TRACE_SimpleSendrecv
    std::cout << "Initialized SimpleSendrecv with collective_size_mb=" << this->collective_size_mb << " where collective_size_from_env_mb is " << collective_size_from_env_mb << " and data_size_mb is " << data_size_mb << " MB, . polled_recv_cnt at qp0 is " << polled_recv_cnt[0] << 
    ". number of msgs per qp is " << this->num_msgs_per_qp << std::endl;
    #endif
    this->marker.assign(A2A_NUM_QPS_PER_RANK, std::vector<int>(this->num_msgs_per_qp, 0));
}

void SimpleSendrecv::inject_init_msgs(sim_request& snd_req, sim_request& rcv_req) {
    start_ts_nano = sys->comm_NI->sim_get_time().time_val;
    #ifdef TRACE_SimpleSendrecv
    std::cout << "First message at timestamp " << start_ts_nano << std::endl;
    #endif
    int init_message_cnt = NUM_INFLIGHT_CHUNKS_PER_QP;
    if (this->num_msgs_per_qp < NUM_INFLIGHT_CHUNKS_PER_QP) {
        init_message_cnt = this->num_msgs_per_qp;
    }
    for (int i = 0; i < init_message_cnt; i++) {
        for (int qp_id = 0; qp_id < A2A_NUM_QPS_PER_RANK; qp_id++) {
            if (is_send) {
                #ifdef TRACE_SimpleSendrecv
                // std::cout << "Send to peer " << r << " for qp " << qp_id << " with tag " << sim_send_cnt[r][qp_id] << std::endl;
                #endif
                snd_req.tag = sim_send_cnt[qp_id]; // also same value as msg_idx;
                sys->front_end_sim_send(
                    0, Sys::dummy_data, P2P_STEP_SIZE, UINT8, peer_rank,
                    qp_id, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE, comm_group_id,
                    &Sys::handleEvent,
                    nullptr);  // stream_id+(packet.preferred_dest*50)
                sim_send_cnt[qp_id]++;
            } else {
                rcv_req.vnet = 0; // Irrelevant
                rcv_req.tag = sim_recv_cnt[qp_id]; // also same value as msg_idx;
                // Encode qp_id value in vnet_id of ehd.
                // Encode (per QP) message count in stream_id of ehd.
                RecvPacketEventHandlerData* ehd = new RecvPacketEventHandlerData(
                    stream, sys->id, EventType::PacketReceived,
                    qp_id, sim_recv_cnt[qp_id]);
                sys->front_end_sim_recv(
                    0, Sys::dummy_data, P2P_STEP_SIZE, UINT8, peer_rank,
                    qp_id, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE, comm_group_id,
                    &Sys::handleEvent,
                    ehd);  // stream_id+(owner->id*50)
                sim_recv_cnt[qp_id]++;
            }
        }
    }
}

void SimpleSendrecv::inject_next_send(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
    snd_req.tag = sim_send_cnt[qp_idx];
    snd_req.vnet = 0; // Irrelevant
    sys->front_end_sim_send(
        0, Sys::dummy_data, P2P_STEP_SIZE, UINT8, peer_rank,
        qp_idx, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE, comm_group_id,
        &Sys::handleEvent,
        nullptr);  // stream_id+(packet.preferred_dest*50)
    sim_send_cnt[qp_idx]++;
}

void SimpleSendrecv::mark_recv_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
    #ifdef TRACE_SimpleSendrecv
    std::cout << " marking recv complete for qp_idx " << qp_idx << ", srcrank is " << rcv_req.srcRank << ", dstrank is " << snd_req.dstRank << ", sim_recv_cnt is " << sim_recv_cnt[qp_idx] << ", polled_recv_cnt is " << polled_recv_cnt[qp_idx] << ", sim_send_cnt is " << sim_send_cnt[qp_idx] << ", polled_send_cnt is " << polled_send_cnt[qp_idx] << std::endl;
    #endif
    int src_rank = rcv_req.srcRank;
    int polled_msg_idx = polled_recv_cnt[qp_idx];
    
    // Safety check: ensure we don't exceed the expected number of messages
    if (polled_msg_idx >= this->num_msgs_per_qp) {
            throw std::runtime_error("WARNING: Rank " + std::to_string(sys->id) + " recv completion spurious: at QP " + std::to_string(qp_idx) + " polled_msg_idx=" + std::to_string(polled_msg_idx) + " >= num_msgs_per_qp=" + std::to_string(this->num_msgs_per_qp) + " (already processed)");
        return;  // Ignore spurious/duplicate completion
    }
    
    polled_recv_cnt[qp_idx]++;
    if (polled_recv_cnt[qp_idx] == this->num_msgs_per_qp) {
        // Assumption: By this time, all sends have been posted
        #ifdef TRACE_SimpleSendrecv
        std::cout << "Marking finished true for peer rank " << src_rank << " and qp_idx " << qp_idx << std::endl;
        #endif
        finished[qp_idx] = true;
        bool all_finished = true;
        for (int i = 0; i < A2A_NUM_QPS_PER_RANK; i++) {
            if (!finished[i]) {
                all_finished = false;
                break;
            }
        }
        if (all_finished) {
            exit();
            return;
        }
        // No more messages to receive for this QP.
        return;
    }

    if (sim_recv_cnt[qp_idx] < this->num_msgs_per_qp) {
        // We still have messages to receive
        rcv_req.vnet = 0; // Irrelevant
        rcv_req.tag = sim_recv_cnt[qp_idx];
        sys->front_end_sim_recv(
            0, Sys::dummy_data, P2P_STEP_SIZE, UINT8, src_rank,
            qp_idx, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE, comm_group_id,
            &Sys::handleEvent,
            nullptr);  // stream_id+(owner->id*50)
        sim_recv_cnt[qp_idx]++;
    }


    marker[qp_idx][polled_msg_idx]++;
    if (marker[qp_idx][polled_msg_idx] == 1) {
    } else if (marker[qp_idx][polled_msg_idx] > 1) {
        // Mark that the send has completed. When the corresponding receive completes, we can inject the next message.
        throw std::runtime_error("Error: Rank " + std::to_string(sys->id) + " polled_recv_cnt has advanced too much at qp_idx " + std::to_string(qp_idx) + " message index " + std::to_string(polled_msg_idx) + ", indicating a logic error in send/recv completion handling.");
    }
}

void SimpleSendrecv::mark_send_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
    #ifdef TRACE_SimpleSendrecv
    std::cout <<  "marking send complete for qp_idx " << qp_idx << ", src rank is " << snd_req.srcRank << ", dst rank is " << snd_req.dstRank << ", sim_send_cnt is " << sim_send_cnt[qp_idx] << ", polled_send_cnt is " << polled_send_cnt[qp_idx] << ", sim_recv_cnt is " << sim_recv_cnt[qp_idx] << ", polled_recv_cnt is " << polled_recv_cnt[qp_idx] << std::endl;
    #endif
    int dst_rank = snd_req.dstRank;
    int polled_msg_idx = polled_send_cnt[qp_idx];
    
    // Safety check: ensure we don't exceed the expected number of messages
    if (polled_msg_idx >= this->num_msgs_per_qp) {
        throw std::runtime_error("WARNING: Rank " + std::to_string(sys->id) + " send completion spurious: at QP " + std::to_string(qp_idx) + " polled_msg_idx=" + std::to_string(polled_msg_idx) + " >= num_msgs_per_qp=" + std::to_string(this->num_msgs_per_qp) + " (already processed)");
        return;  // Ignore spurious/duplicate completion
    }
    
    polled_send_cnt[qp_idx]++;
    if (polled_send_cnt[qp_idx] == this->num_msgs_per_qp) {
        // Assumption: By this time, all sends have been posted
        #ifdef TRACE_SimpleSendrecv
        std::cout << "Marking finished true for peer rank " << dst_rank << " and qp_idx " << qp_idx << std::endl;
        #endif
        finished[qp_idx] = true;
        bool all_finished = true;
        for (int i = 0; i < A2A_NUM_QPS_PER_RANK; i++) {
            if (!finished[i]) {
                all_finished = false;
                break;
            }
        }
        if (all_finished) {
            exit();
            return;
        }
        return;
    }
    marker[qp_idx][polled_msg_idx]++;
    if (marker[qp_idx][polled_msg_idx] == 1) {
        if (sim_send_cnt[qp_idx] < this->num_msgs_per_qp) {
            // Still more send messages to be sent.
            inject_next_send(qp_idx, snd_req, rcv_req);
        } else {
            #if TRACE_SimpleSendrecv
                std::cout << "All messages have been sent for qp_idx " << qp_idx << ". No more sends to inject." << std::endl;
            #endif
        }
    } else if (marker[qp_idx][polled_msg_idx] > 1) {
        // Mark that the send has completed. When the corresponding receive completes, we can inject the next message.
        throw std::runtime_error("Error: Rank " + std::to_string(sys->id) + " polled_send_cnt has advanced too much at qp_idx " + std::to_string(qp_idx) + " message index " + std::to_string(polled_msg_idx) + ", indicating a logic error in send/recv completion handling.");
    }
}

void SimpleSendrecv::run(EventType event, CallData* data) {
    sim_request snd_req;
    snd_req.srcRank = id;
    snd_req.dstRank = peer_rank;
    snd_req.reqType = UINT8;
    snd_req.vnet = 0; // Irrelevant

    sim_request rcv_req;
    rcv_req.srcRank = peer_rank;
    rcv_req.reqType = UINT8;


    if (event == EventType::StreamInit) {
        #ifdef TRACE_SimpleSendrecv
        std::cout << "Running SimpleSendrecv for StreamInit event. is_send=" <<
        is_send << std::endl;
        #endif
        inject_init_msgs(snd_req, rcv_req);
    } else if (event == EventType::PacketReceived) {
        throw std::runtime_error("Error: PacketReceived event should be handled in mark_recv_complete with EHD, not in run() directly.");
        // inject_next_msg(static_cast<RecvPacketEventHandlerData*>(data), snd_req, rcv_req);
    }
}

void SimpleSendrecv::record_stats() {
    // Compute and report throughput.
    Tick end_ts_nano = sys->comm_NI->sim_get_time().time_val;
    int elapsed_ns = static_cast<int>(end_ts_nano - start_ts_nano);
    sys->stat_counter->record_ring_coll(elapsed_ns, collective_size_mb, A2A_NUM_QPS_PER_RANK, num_msgs_per_qp, AstraSim::ComType::None);
}

void SimpleSendrecv::exit() {
    if (getenv("GDB_DEBUG") != nullptr && id != 0) {
        sleep(300);
    }

    record_stats();
    sys->unload_genie_collective();
    sys->workload->call(EventType::General, (CallData*)wlhd);
    // sys->proceed_to_next_vnet_baseline((StreamBaseline*)stream);

    return;
}