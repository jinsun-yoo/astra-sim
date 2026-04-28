#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/SimpleRing.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"
#include <unistd.h>

using namespace AstraSim;

// Static member variable definition
int SimpleRing::collective_size_from_env_mb = -1;

void SimpleRing::get_collective_size_from_env() {
    if (collective_size_from_env_mb == -1) {
        const char* env_str = getenv("GENIE_SIMPLERING_COLLECTIVE_SIZE_MB");
        collective_size_from_env_mb = (env_str && *env_str != '\0') ? std::stoi(env_str) : 0;
        std::cout << "SimpleRing: collective_size_from_env_mb is set to " << collective_size_from_env_mb << " MB based on environment variable GENIE_SIMPLERING_COLLECTIVE_SIZE_MB=" << (env_str ? env_str : "null") << std::endl;
    }
    return;
}

SimpleRing::SimpleRing(int id, uint64_t data_size_bytes, ComType collective_type): GenieCollective() {
    this->id = id;
    this->send_dst = (id + 1) % NUM_RANKS;
    this->recv_src = (id - 1 + NUM_RANKS) % NUM_RANKS;
    this->collective_type = collective_type;
    get_collective_size_from_env();
    int data_size_mb = data_size_bytes / (1024 * 1024);
    this->collective_size_mb = collective_size_from_env_mb ? collective_size_from_env_mb : data_size_mb;
    // If 2G buffer, we need 1G per QP (2G / 2), and 256MB per rank (1G / NUM_RANKS). 
    // Because this is AllReduce Ring, each rank sends (NUM_RANKS - 1) * 2 times, hence the '*6'.
    if (collective_type == ComType::All_Reduce) {
        this->num_msgs_per_qp = (this->collective_size_mb  / (NUM_QPS * NUM_RANKS * MSG_SIZE_MB)) * 6;
    } else {
        this->num_msgs_per_qp = (this->collective_size_mb  / (NUM_QPS * NUM_RANKS * MSG_SIZE_MB)) * 3;
    }
    // This macro is defined in the top CMakeLists.txt
    #ifdef TRACE_SIMPLERING
    std::cout << "Initialized SimpleRing with collective_size_mb=" << this->collective_size_mb << " where collective_size_from_env_mb is " << collective_size_from_env_mb << " and data_size_mb is " << data_size_mb << " MB, "
    "send_dst=" << send_dst << ", recv_src=" << recv_src << ". polled_recv_cnt at qp0 is " << polled_recv_cnt[0] << 
    ". number of msgs per qp is " << this->num_msgs_per_qp << std::endl;
    #endif
    this->marker.assign(NUM_QPS, std::vector<int>(this->num_msgs_per_qp, 0));
}

void SimpleRing::inject_init_msgs(sim_request& snd_req, sim_request& rcv_req) {
    start_ts_nano = stream->owner->comm_NI->sim_get_time().time_val;
    #ifdef TRACE_SIMPLERING
    std::cout << "First message at timestamp " << start_ts_nano << std::endl;
    #endif
    int init_message_cnt = NUM_INFLIGHT_CHUNKS_PER_QP;
    if (this->num_msgs_per_qp < NUM_INFLIGHT_CHUNKS_PER_QP) {
        init_message_cnt = this->num_msgs_per_qp;
    }
    for (int i = 0; i < init_message_cnt; i++) {
        for (int qp_id = 0; qp_id < NUM_QPS; qp_id++) {
            snd_req.tag = sim_send_cnt[qp_id]; // also same value as msg_idx;
            stream->owner->front_end_sim_send(
                0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, send_dst,
                qp_id, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE,
                &Sys::handleEvent,
                nullptr);  // stream_id+(packet.preferred_dest*50)
            sim_send_cnt[qp_id]++;
            

            rcv_req.vnet = 0; // Irrelevant
            rcv_req.tag = sim_recv_cnt[qp_id]; // also same value as msg_idx;
            // Encode qp_id value in vnet_id of ehd.
            // Encode (per QP) message count in stream_id of ehd.
            RecvPacketEventHandlerData* ehd = new RecvPacketEventHandlerData(
                stream, stream->owner->id, EventType::PacketReceived,
                qp_id, sim_recv_cnt[qp_id]);
            stream->owner->front_end_sim_recv(
                0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, recv_src,
                qp_id, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
                &Sys::handleEvent,
                ehd);  // stream_id+(owner->id*50)
            sim_recv_cnt[qp_id]++;
        }
    }
}
/*
void SimpleRing::inject_next_msg(RecvPacketEventHandlerData *data, sim_request& snd_req, sim_request& rcv_req) {
    int qp_idx = data->vnet;
    polled_recv_cnt[qp_idx]++;
    if (polled_recv_cnt[qp_idx] == this->num_msgs_per_qp) {
        finished[qp_idx] = true;
        bool all_finished = true;
        for (int i = 0; i < NUM_QPS; i++) {
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
    if (sim_send_cnt[qp_idx] == this->num_msgs_per_qp || sim_recv_cnt[qp_idx] == this->num_msgs_per_qp) {
        // No more messages to send. Wait for other messages to complete. 
        return;
    }
    
    snd_req.tag = sim_send_cnt[qp_idx];
    snd_req.vnet = 0; // Irrelevant
    stream->owner->front_end_sim_send(
        0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, send_dst,
        qp_idx, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        &Sys::handleEvent,
        nullptr);  // stream_id+(packet.preferred_dest*50)
    sim_send_cnt[qp_idx]++;
    

    rcv_req.vnet = 0; // Irrelevant
    rcv_req.tag = sim_recv_cnt[qp_idx];
    RecvPacketEventHandlerData* ehd = new RecvPacketEventHandlerData(
        stream, stream->owner->id, EventType::PacketReceived,
        qp_idx, sim_recv_cnt[qp_idx]);
    stream->owner->front_end_sim_recv(
        0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, recv_src,
        qp_idx, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        &Sys::handleEvent,
        ehd);  // stream_id+(owner->id*50)
    sim_recv_cnt[qp_idx]++;
}
    */

void SimpleRing::inject_next_send(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
    snd_req.tag = sim_send_cnt[qp_idx];
    snd_req.vnet = 0; // Irrelevant
    stream->owner->front_end_sim_send(
        0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, send_dst,
        qp_idx, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        &Sys::handleEvent,
        nullptr);  // stream_id+(packet.preferred_dest*50)
    sim_send_cnt[qp_idx]++;
}

void SimpleRing::mark_recv_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
    // std::cout << " marking recv complete for qp_idx " << qp_idx << ", sim_recv_cnt is " << sim_recv_cnt[qp_idx] << ", polled_recv_cnt is " << polled_recv_cnt[qp_idx] << std::endl;
    int polled_msg_idx = polled_recv_cnt[qp_idx];
    
    // Safety check: ensure we don't exceed the expected number of messages
    if (polled_msg_idx >= this->num_msgs_per_qp) {
            throw std::runtime_error("WARNING: Rank " + std::to_string(stream->owner->id) + " recv completion spurious: at QP " + std::to_string(qp_idx) + " polled_msg_idx=" + std::to_string(polled_msg_idx) + " >= num_msgs_per_qp=" + std::to_string(this->num_msgs_per_qp) + " (already processed)");
        return;  // Ignore spurious/duplicate completion
    }
    
    polled_recv_cnt[qp_idx]++;
    if (polled_recv_cnt[qp_idx] == this->num_msgs_per_qp && polled_send_cnt[qp_idx] == this->num_msgs_per_qp) {
        // Assumption: By this time, all sends have been posted
        finished[qp_idx] = true;
        bool all_finished = true;
        for (int i = 0; i < NUM_QPS; i++) {
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
        stream->owner->front_end_sim_recv(
            0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, recv_src,
            qp_idx, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
            &Sys::handleEvent,
            nullptr);  // stream_id+(owner->id*50)
        sim_recv_cnt[qp_idx]++;
    }


    marker[qp_idx][polled_msg_idx]++;
    if (marker[qp_idx][polled_msg_idx] == 2) {
        if (sim_send_cnt[qp_idx] < this->num_msgs_per_qp) {
            // Still more send messages to be sent.
            inject_next_send(qp_idx, snd_req, rcv_req);
        }
    } else if (marker[qp_idx][polled_msg_idx] > 2) {
        // Mark that the send has completed. When the corresponding receive completes, we can inject the next message.
        throw std::runtime_error("Error: Rank " + std::to_string(stream->owner->id) + " polled_recv_cnt has advanced too much at qp_idx " + std::to_string(qp_idx) + " message index " + std::to_string(polled_msg_idx) + ", indicating a logic error in send/recv completion handling.");
    }
}

void SimpleRing::mark_send_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
        // std::cout <<  "marking send complete for qp_idx " << qp_idx << ", sim_send_cnt is " << sim_send_cnt[qp_idx] << ", polled_send_cnt is " << polled_send_cnt[qp_idx] << std::endl;
    int polled_msg_idx = polled_send_cnt[qp_idx];
    
    // Safety check: ensure we don't exceed the expected number of messages
    if (polled_msg_idx >= this->num_msgs_per_qp) {
        throw std::runtime_error("WARNING: Rank " + std::to_string(stream->owner->id) + " send completion spurious: at QP " + std::to_string(qp_idx) + " polled_msg_idx=" + std::to_string(polled_msg_idx) + " >= num_msgs_per_qp=" + std::to_string(this->num_msgs_per_qp) + " (already processed)");
        return;  // Ignore spurious/duplicate completion
    }
    
    polled_send_cnt[qp_idx]++;
    if (polled_recv_cnt[qp_idx] == this->num_msgs_per_qp && polled_send_cnt[qp_idx] == this->num_msgs_per_qp) {
        // Assumption: By this time, all sends have been posted
        finished[qp_idx] = true;
        bool all_finished = true;
        for (int i = 0; i < NUM_QPS; i++) {
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
    marker[qp_idx][polled_msg_idx]++;
    if (marker[qp_idx][polled_msg_idx] == 2) {
        if (sim_send_cnt[qp_idx] < this->num_msgs_per_qp) {
            // Still more send messages to be sent.
            inject_next_send(qp_idx, snd_req, rcv_req);
        }
    } else if (marker[qp_idx][polled_msg_idx] > 2) {
        // Mark that the send has completed. When the corresponding receive completes, we can inject the next message.
        throw std::runtime_error("Error: Rank " + std::to_string(stream->owner->id) + " polled_send_cnt has advanced too much at qp_idx " + std::to_string(qp_idx) + " message index " + std::to_string(polled_msg_idx) + ", indicating a logic error in send/recv completion handling.");
    }
}

/*
void SimpleRing::inject_next_msg_no_ehd(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
    polled_recv_cnt[qp_idx]++;
    if (polled_recv_cnt[qp_idx] == this->num_msgs_per_qp) {
        finished[qp_idx] = true;
        bool all_finished = true;
        for (int i = 0; i < NUM_QPS; i++) {
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
    if (sim_send_cnt[qp_idx] == this->num_msgs_per_qp || sim_recv_cnt[qp_idx] == this->num_msgs_per_qp) {
        // No more messages to send. Wait for other messages to complete. 
        return;
    }
    
    snd_req.tag = sim_send_cnt[qp_idx];
    snd_req.vnet = 0; // Irrelevant
    stream->owner->front_end_sim_send(
        0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, send_dst,
        qp_idx, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        &Sys::handleEvent,
        nullptr);  // stream_id+(packet.preferred_dest*50)
    sim_send_cnt[qp_idx]++;
    

    rcv_req.vnet = 0; // Irrelevant
    rcv_req.tag = sim_recv_cnt[qp_idx];
    stream->owner->front_end_sim_recv(
        0, Sys::dummy_data, MSG_SIZE_MB * 1024 * 1024, UINT8, recv_src,
        qp_idx, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        &Sys::handleEvent,
        nullptr);  // stream_id+(owner->id*50)
    sim_recv_cnt[qp_idx]++;
}
    */

void SimpleRing::run(EventType event, CallData* data) {
    sim_request snd_req;
    snd_req.srcRank = id;
    snd_req.dstRank = send_dst;
    snd_req.reqType = UINT8;
    snd_req.vnet = 0; // Irrelevant

    sim_request rcv_req;
    rcv_req.srcRank = recv_src;
    rcv_req.reqType = UINT8;


    if (event == EventType::StreamInit) {
        inject_init_msgs(snd_req, rcv_req);
    } else if (event == EventType::PacketReceived) {
        throw std::runtime_error("Error: PacketReceived event should be handled in mark_recv_complete with EHD, not in run() directly.");
        // inject_next_msg(static_cast<RecvPacketEventHandlerData*>(data), snd_req, rcv_req);
    }
}

void SimpleRing::record_stats() {
    // Compute and report throughput.
    Tick end_ts_nano = stream->owner->comm_NI->sim_get_time().time_val;
    int elapsed_ns = static_cast<int>(end_ts_nano - start_ts_nano);
    stream->owner->stat_counter->record_ring_coll(elapsed_ns, collective_size_mb, NUM_QPS, num_msgs_per_qp, collective_type);
}

void SimpleRing::exit() {
    if (getenv("GDB_DEBUG") != nullptr && id != 0) {
        sleep(300);
    }

    record_stats();
    stream->owner->unload_genie_collective();
    stream->owner->proceed_to_next_vnet_baseline((StreamBaseline*)stream);

    return;
}