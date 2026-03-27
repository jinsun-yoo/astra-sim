#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/SimpleRing.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"
#include <unistd.h>

using namespace AstraSim;

SimpleRing::SimpleRing(int id): Algorithm() {
    this->id = id;
    this->send_dst = (id + 1) % NUM_RANKS;
    this->recv_src = (id - 1 + NUM_RANKS) % NUM_RANKS;
    auto collective_size_env = getenv("GENIE_SIMPLERING_COLLECTIVE_SIZE_MB");
    this->collective_size_mb = collective_size_env ? std::stoi(collective_size_env) : 2048;
    // If 2G buffer, we need 1G per QP (2G / 2), and 256MB per rank (1G / NUM_RANKS). 
    // Because this is AllReduce Ring, each rank sends (NUM_RANKS - 1) * 2 times, hence the '*6'.
    this->num_msgs_per_qp = (this->collective_size_mb  / (NUM_QPS * NUM_RANKS * MSG_SIZE_MB)) * 6;
    this->marker.assign(NUM_QPS, std::vector<int>(this->num_msgs_per_qp, 0));
}

void SimpleRing::inject_init_msgs(sim_request& snd_req, sim_request& rcv_req) {
    for (int i = 0; i < NUM_INFLIGHT_CHUNKS_PER_QP; i++) {
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
    int polled_msg_idx = polled_recv_cnt[qp_idx];
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
        throw std::runtime_error("Error: polled_send_cnt has advanced too much, indicating a logic error in send/recv completion handling.");
    }
}

void SimpleRing::mark_send_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) {
    int polled_msg_idx = polled_send_cnt[qp_idx];
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
        throw std::runtime_error("Error: polled_send_cnt has advanced too much, indicating a logic error in send/recv completion handling.");
    }
}

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

    // Compute and report throughput.
    AstraSim::timespec_t ts = stream->owner->comm_NI->sim_get_time();
    double elapsed_s = ts.time_val / 1.0e9;
    // busbw = (collective_size / time) * 2*(N-1)/N  (per-link throughput, standard NCCL metric)
    double data_gb = collective_size_mb / 1024.0;
    double busbw_gbs = (elapsed_s > 0) ? data_gb / elapsed_s * 2.0 * (NUM_RANKS - 1) / NUM_RANKS : 0;
    int total_msgs = num_msgs_per_qp * NUM_QPS;
    double msgrate = (elapsed_s > 0) ? total_msgs / elapsed_s : 0;
    std::cout << "[Rank " << id << "] elapsed=" << elapsed_s
              << "s size=" << collective_size_mb
              << "MB busbw=" << busbw_gbs << " GB/s ("
              << busbw_gbs * 8 << " Gbps)"
              << " msgrate=" << msgrate / 1e6 << " Mpps" << std::endl;

    stream->owner->proceed_to_next_vnet_baseline((StreamBaseline*)stream);

    return;
}