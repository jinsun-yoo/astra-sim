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
static constexpr size_t BUF_SIZE = (1ULL << 32); // 4GB

static constexpr int GENIE_RECV_WR_PREPOST = 16;

QueuepairManager::QueuepairManager(std::shared_ptr<gloo::transport::Context> context, std::shared_ptr<spdlog::logger> logger, int send_id, int recv_id, int nqps) {
    _context = context;
    _logger = logger;
    if (BUF_SIZE == 0) {
        throw std::runtime_error("Invalid BUF_SIZE: 0");
    }
    auto cycle_buffer = sysconf(_SC_PAGESIZE);
    int rank = (recv_id + 1 ) % context->size; // Get rank from recv_id. This is based on the hardcoded assumption of a ring topology.
    std::cout << "Initializing QueuepairManager for rank " << rank << " with send_id " << send_id << ", recv_id " << recv_id << ", and " << nqps << " QPs." << std::endl;
    for (int qp_idx = 0; qp_idx < nqps; ++qp_idx) {
        // Hardcode for bidirectional ring
        send_id = (rank + 1) % context->size;
        recv_id = (rank - 1 + context->size) % context->size;
        // End hardcode
        const auto&send_pair = _context->getPair(send_id, qp_idx);
        send_pair->setSync(true, true);
        const auto&recv_pair = _context->getPair(recv_id, qp_idx);
        recv_pair->setSync(true, true);
        // Allocate a buffer in memory for send operations. Note 'buffer' is different from gloo::transport::Buffer.
        void *send_buf_addr = memalign(cycle_buffer, BUF_SIZE);
        void *recv_buf_addr = memalign(cycle_buffer, BUF_SIZE);
        if (send_buf_addr == nullptr || recv_buf_addr == nullptr) {
            throw std::runtime_error("memalign failed while allocating Genie QP buffers");
        }
        if (rank == 0) {
            std::cout << "Rank " << rank << " QP " << qp_idx << " allocated send/recv buffers of "
                    << BUF_SIZE << " bytes" << std::endl;
        }
        // Create a memory region for send and receive buffers.
        // Only one buffer per QP for now.
        auto send_buffer_ptr = send_pair->createSendBuffer(0, send_buf_addr, BUF_SIZE);
        auto send_buffer = send_buffer_ptr.release();
        // auto send_buffer = static_cast<gloo::transport::ibverbs::Buffer*>(send_buffer_ptr.release());
        send_buffers.emplace_back(send_buffer);

        auto recv_buffer_ptr = recv_pair->createRecvBuffer(0, recv_buf_addr, BUF_SIZE);
        auto recv_buffer = recv_buffer_ptr.release();
        // auto recv_buffer = static_cast<gloo::transport::ibverbs::Buffer*>(recv_buffer_ptr.release());
        recv_buffers.emplace_back(recv_buffer);
        // Issue 37. Poll one initial send operation to this QP.
        recv_buffer->pollQP();

        // Pre-post recv WRs for the initial message burst.
        // Steady-state backfill is handled by poll_recv_handler.
        for (int r = 0; r < GENIE_RECV_WR_PREPOST; r++) {
            int buf_idx = r & 3; // Using last 2 bits b/c we have 4 offsets RR.
            recv_buffer->recv(5000 + r, buf_idx * MSG_SIZE_MB * 1024 * 1024, MSG_SIZE_MB * 1024 * 1024);
        }
    }
}

// TODO: All of the functions below have been commented out in favor of hardcoded debugging. (i.e. 1 buffer per QP and 1 QP per rank pair)
std::unique_ptr<gloo::transport::Buffer> QueuepairManager::fetch_queue(bool is_send_queue) {
    _logger->critical("QueuepairManager::fetch_queue is not implemented yet. This function is not used in ASTRASimGenieNetwork.");
    exit(1);
    // std::lock_guard<std::mutex> lock(_mutex);
    // if (is_send_queue && send_queues.size() == 0 || 
    // !is_send_queue && receive_queues.size() == 0) {
    //     throw std::runtime_error("No send queues available");
    // }

    // if (is_send_queue) {
    //     std::unique_ptr<gloo::transport::Buffer> front = std::move(send_queues.front());
    //     send_queues.pop();
    //     return std::move(front);
    // } else {
    //     std::unique_ptr<gloo::transport::Buffer> front = std::move(receive_queues.front());
    //     receive_queues.pop();
    //     return std::move(front);
    // }
}

void QueuepairManager::dismiss_queue(std::unique_ptr<gloo::transport::Buffer> buffer, bool is_send_queue) {
    _logger->critical("QueuepairManager::dismiss_queue is not implemented yet. This function is not used in ASTRASimGenieNetwork.");
    exit(1);
    // std::lock_guard<std::mutex> lock(_mutex);
    // if (is_send_queue) {
    //     send_queues.push(std::move(buffer));
    // } else {
    //     receive_queues.push(std::move(buffer));
    // }
}

std::unique_ptr<gloo::transport::Buffer> QueuepairManager::fetch_buffer(int idx, bool is_send_buffer) {
    _logger->critical("QueuepairManager::fetch_buffer is not implemented yet. This function is not used in ASTRASimGenieNetwork.");
    exit(1);
    //std::lock_guard<std::mutex> lock(_mutex);
    
    // if (is_send_buffer && send_buffers.size() == 0 || 
    // !is_send_buffer && recv_buffers.size() == 0) {
    //     throw std::runtime_error("No send buffers available");
    // }

    // if (is_send_buffer) {
    //     return std::move(send_buffers[idx]);
    // } else {
    //     return std::move(recv_buffers[idx]);
    // }
}

void QueuepairManager::dismiss_buffer(std::unique_ptr<gloo::transport::Buffer> buffer, int idx, bool is_send_buffer) {
    _logger->critical("QueuepairManager::dismiss_buffer is not implemented yet. This function is not used in ASTRASimGenieNetwork.");
    exit(1);
    //std::lock_guard<std::mutex> lock(_mutex);
    // if (buffer == nullptr) {
    //     throw std::runtime_error("Buffer is null");
    // }
    // if (is_send_buffer) {
    //     send_buffers[idx] = std::move(buffer);
    // } else {
    //     recv_buffers[idx] = std::move(buffer);
    // }
}