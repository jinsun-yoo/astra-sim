#include <queue>
#include <stdexcept>
#include <mutex>
#include <malloc.h>
#include <unistd.h>
#include <string>

#include "qp_manager.hh"
#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/SimpleRing.hh"

// TODO: Assume only 1 QP per rank, and 1 Buffer per QP.
#define NUM_BUFS 1

#define CTS_SIZE sizeof(CTSEntry)
#define NUM_CTS_ENTRY 256

static constexpr size_t BUF_SIZE = (1ULL << 32); // 4GB

static constexpr int GENIE_RECV_WR_PREPOST = 16;

QueuepairManager::QueuepairManager(std::shared_ptr<gloo::transport::Context> context, std::shared_ptr<spdlog::logger> logger, int rank, int nqps) {
    _context = context;
    _logger = logger;
    this->rank = rank;
    this->nranks = context->size;
    this->nqps = nqps;
    if (BUF_SIZE == 0) {
        throw std::runtime_error("Invalid BUF_SIZE: 0");
    }
    auto cycle_buffer = sysconf(_SC_PAGESIZE);
    std::cout << "Initializing QueuepairManager for rank " << rank << ", and " << nqps << " QPs. Buffer size " << BUF_SIZE << std::endl;
    for (int peer = 0; peer < context->size; peer++) {
        std::cout << "Rank " << rank << " sees peer " << peer << std::endl;
        if (peer == rank) {
            std::cout << "Skip" << std::endl;
            // Push placeholder nulls so that peer_rank * nqps + qp_idx indexing stays
            // correct for all peers with rank > self.
            for (int qp_idx = 0; qp_idx < nqps; ++qp_idx) {
                send_buffers.emplace_back(nullptr);
                recv_buffers.emplace_back(nullptr);
                cts_recv_buffers.emplace_back(nullptr);
                cts_recv_ptrs.emplace_back(nullptr);
                recv_ctx_next_idx.push_back(0);
                cts_send_buffers.emplace_back(nullptr);
                cts_send_ptrs.emplace_back(nullptr);
                send_ctx_next_idx.push_back(0);
                send_buf_addrs.emplace_back(nullptr);
                recv_buf_addrs.emplace_back(nullptr);
            }
            continue;
        }
        for (int qp_idx = 0; qp_idx < nqps; ++qp_idx) {
            // There are 4 x nqps QPs that Gloo sees. The first nqps are used for rank to send to peer, the second nqps are used for rank to recv from peer.
            const auto&send_pair = _context->getPair(peer, qp_idx);
            send_pair->setSync(true, true);
            int receive_qp_idx = qp_idx + nqps; 
            const auto&recv_pair = _context->getPair(peer, receive_qp_idx);
            recv_pair->setSync(true, true);

            // Allocate a buffer in memory for send operations. Note 'buffer' is different from gloo::transport::Buffer.
            void *send_buf_addr = memalign(cycle_buffer, BUF_SIZE);
            void *recv_buf_addr = memalign(cycle_buffer, BUF_SIZE);
            if (send_buf_addr == nullptr || recv_buf_addr == nullptr) {
                throw std::runtime_error("memalign failed while allocating Genie QP buffers");
            }
            // Create a memory region for send and receive buffers.
            // Only one buffer per QP for now.
            auto send_buffer_ptr = send_pair->createSendBuffer(0, send_buf_addr, BUF_SIZE);
            auto send_buffer = send_buffer_ptr.release();
            send_buffers.emplace_back(send_buffer);

            auto recv_buffer_ptr = recv_pair->createRecvBuffer(0, recv_buf_addr, BUF_SIZE);
            auto recv_buffer = recv_buffer_ptr.release();
            recv_buffers.emplace_back(recv_buffer);
            // Issue 37. Poll one initial send operation to this QP.
            recv_buffer->pollQP();

            if (IS_PINGPONG) {
                throw std::runtime_error("Have not figured how to handle CTS with pingpong yet.");
            }
            // There are 4 x nqps QPs that Gloo sees. The fourth nqps are used for rank to send CTS messages to peer, the third nqps are used for rank to receive CTS messages from peer.
            int receive_cts_qp_idx = qp_idx + 2 * nqps;
            const auto&recv_cts_pair = _context->getPair(peer, receive_cts_qp_idx);
            recv_cts_pair->setSync(true, true);
            int send_cts_qp_idx = qp_idx + 3 * nqps;
            const auto&send_cts_pair = _context->getPair(peer, send_cts_qp_idx);
            send_cts_pair->setSync(true, true);

            CTSEntry *cts_send_buf_addr = static_cast<CTSEntry*>(memalign(cycle_buffer, NUM_CTS_ENTRY * CTS_SIZE));
            CTSEntry *cts_recv_buf_addr = static_cast<CTSEntry*>(memalign(cycle_buffer, NUM_CTS_ENTRY * CTS_SIZE));
            if (cts_send_buf_addr == nullptr || cts_recv_buf_addr == nullptr) {
                throw std::runtime_error("memalign failed while allocating Genie QP buffers");
            }
            for (int i = 0; i < NUM_CTS_ENTRY; i++) {
                cts_send_buf_addr[i].stream_id = -1; // Initialize stream_id to -1 to indicate no message.
                cts_recv_buf_addr[i].stream_id = -1;
            }

            auto cts_recv_buffer_ptr = send_cts_pair->createRecvBuffer(0, cts_recv_buf_addr, NUM_CTS_ENTRY * CTS_SIZE);
            auto cts_recv_buffer = cts_recv_buffer_ptr.release();
            cts_recv_buffers.emplace_back(cts_recv_buffer);
            cts_recv_ptrs.emplace_back(cts_recv_buf_addr);
            cts_recv_buffer->pollQP();
            recv_ctx_next_idx.push_back(0);

            auto cts_send_buffer_ptr = recv_cts_pair->createSendBuffer(0, cts_send_buf_addr, NUM_CTS_ENTRY * CTS_SIZE);
            auto cts_send_buffer = cts_send_buffer_ptr.release();
            cts_send_buffers.emplace_back(cts_send_buffer);
            cts_send_ptrs.emplace_back(cts_send_buf_addr);
            send_ctx_next_idx.push_back(0);


            // Pre-post recv WRs for the initial message burst.
            // Steady-state backfill is handled by poll_recv_handler.
            for (int r = 0; r < GENIE_RECV_WR_PREPOST; r++) {
                int buf_idx = r & 3; // Using last 2 bits b/c we have 4 offsets RR.
                recv_buffer->recv(5000 + r, buf_idx * MSG_SIZE_MB * 1024 * 1024, MSG_SIZE_MB * 1024 * 1024);
            }
            std::cout << "Rank " << rank << " initialized send QP " << qp_idx << " and recv QP " << receive_qp_idx << " and send cts qp " << send_cts_qp_idx << " and recv cts qp " << receive_cts_qp_idx << " for peer " << peer << std::endl;
        }
    }
}

void QueuepairManager::send_cts_message(int peer_rank, int qp_idx, int stream_id) {
    int next_send_idx = send_ctx_next_idx[peer_rank * nqps + qp_idx];
    auto* cts_entries = cts_send_ptrs[peer_rank * nqps + qp_idx];
    cts_entries[next_send_idx] = CTSEntry{stream_id, qp_idx};
    // std::cout << "Rank " << _context->rank << " sending CTS message for stream_id " << stream_id << " on QP " << qp_idx << " at send index " << next_send_idx << std::endl;
    cts_send_buffers[peer_rank * nqps + qp_idx]->send(next_send_idx * CTS_SIZE, CTS_SIZE, next_send_idx * CTS_SIZE, -1);
    send_ctx_next_idx[peer_rank * nqps + qp_idx] = (next_send_idx + 1) % NUM_CTS_ENTRY;
}

void QueuepairManager::poll_send_cts_complete(int peer_rank, int qp_idx) {
    cts_send_buffers[peer_rank * nqps + qp_idx]->pollQP();
}

int QueuepairManager::check_incoming_cts(int peer_rank, int qp_idx) {
    int next_recv_idx = recv_ctx_next_idx[peer_rank * nqps + qp_idx];
    auto* cts_entries = cts_recv_ptrs[peer_rank * nqps + qp_idx];
    // Poll for the next CTS message. If available, return the stream_id. Otherwise,
    // return -1 to indicate no message.
    CTSEntry entry = cts_entries[next_recv_idx];
    int marked_stream_id = entry.stream_id;
    // std::cout << "Rank " << _context->rank << " polled CTS message for QP " << qp_idx << " at recv index " << next_recv_idx << " with stream_id " << marked_stream_id << std::endl;
    if (marked_stream_id != -1) {
        // Mark this entry as consumed by resetting stream_id to -1.
        cts_entries[next_recv_idx].stream_id = -1;
        recv_ctx_next_idx[peer_rank * nqps + qp_idx] = (next_recv_idx + 1) % NUM_CTS_ENTRY;
        return marked_stream_id;
    } else {
        return -1; // No message available
    }
}
