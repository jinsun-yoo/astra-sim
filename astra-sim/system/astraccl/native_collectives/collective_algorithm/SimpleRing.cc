#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/SimpleRing.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"

using namespace AstraSim;
#define NUM_CHUNKS_PER_QP 4 
#define MSG_SIZE 1048576
#define NUM_RANKS 4

SimpleRing::SimpleRing(int id): Algorithm() {
    this->id = id;
    this->send_dst = (id + 1) % NUM_RANKS;
    this->recv_src = (id - 1 + NUM_RANKS) % NUM_RANKS;
    auto collective_size_env = getenv("GENIE_SIMPLERING_COLLECTIVE_SIZE_MB");
    this->collective_size_mb = collective_size_env ? std::stoi(collective_size_env) : 2048;
    // If 2G buffer, we need 1G per QP (2G / 2), and 256MB per rank (1G / NUM_RANKS). 
    // Because this is AllReduce Ring, each rank sends (NUM_RANKS - 1) * 2 times, hence the '*6'.
    this->num_msgs_per_qp = (this->collective_size_mb / (NUM_QPS * NUM_RANKS)) * 6; 
    if (id == 0) {
         std::cout << "SimpleRing initialized with collective size " << this->collective_size_mb << " MB, " << this->num_msgs_per_qp << " messages per QP." << std::endl;
    }
}

void SimpleRing::inject_init_msgs(sim_request& snd_req, sim_request& rcv_req) {
    for (int i = 0; i < NUM_CHUNKS_PER_QP; i++) {
        for (int qp_id = 0; qp_id < 2; qp_id++) {
            snd_req.tag = sim_send_cnt[qp_id]; // also same value as msg_idx;
            stream->owner->front_end_sim_send(
                0, Sys::dummy_data, MSG_SIZE, UINT8, send_dst,
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
                0, Sys::dummy_data, MSG_SIZE, UINT8, recv_src,
                qp_id, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
                &Sys::handleEvent,
                ehd);  // stream_id+(owner->id*50)
            sim_recv_cnt[qp_id]++;
        }
    }
}

void SimpleRing::inject_next_msg(RecvPacketEventHandlerData *data, sim_request& snd_req, sim_request& rcv_req) {
    int qp_idx = data->vnet;
    polled_recv_cnt[qp_idx]++;
    if (polled_recv_cnt[qp_idx] == this->num_msgs_per_qp) {
        finished[qp_idx] = true;
        if (finished[0] && finished[1]) {
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
        0, Sys::dummy_data, MSG_SIZE, UINT8, send_dst,
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
        0, Sys::dummy_data, MSG_SIZE, UINT8, recv_src,
        qp_idx, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        &Sys::handleEvent,
        ehd);  // stream_id+(owner->id*50)
    sim_recv_cnt[qp_idx]++;
}

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
        inject_next_msg(static_cast<RecvPacketEventHandlerData*>(data), snd_req, rcv_req);
    }
}

void SimpleRing::exit() {
    if (getenv("GDB_DEBUG") != nullptr && id != 0) {
        sleep(300);
    }
    stream->owner->proceed_to_next_vnet_baseline((StreamBaseline*)stream);
    
    // With consolidated polling, we no longer can exit Genie's event queue.
    // This is a hardcoded approach. Ideally, we will mark a variable that the event queue checks 
    // in event_queue.cc.
    std::exit(0);
    return;
}