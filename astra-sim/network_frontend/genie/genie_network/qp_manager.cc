#include <queue>
#include <stdexcept>
#include <mutex>
#include <malloc.h>
#include <unistd.h>
#include <string>

#include "qp_manager.hh"

// TODO: Assume only 1 QP per rank, and 1 Buffer per QP. 
#define NUM_BUFS 2
#define BUF_SIZE 1 << 28 // 256MB

QueuepairManager::QueuepairManager(std::shared_ptr<gloo::transport::Context> context, std::shared_ptr<spdlog::logger> logger, int send_id, int recv_id) {
    _context = context;
    _logger = logger;
    auto cycle_buffer = sysconf(_SC_PAGESIZE);
    const auto&send_pair = _context->getPair(send_id);
    send_pair->setSync(true, true);
    const auto&recv_pair = _context->getPair(recv_id);
    recv_pair->setSync(true, true);
    for (int i =0; i < NUM_BUFS; ++i) {
        // Allocate a buffer in memory for send operations. Note 'buffer' is different from gloo::transport::Buffer.
        void *send_buf_addr = memalign(cycle_buffer, BUF_SIZE);
        void *recv_buf_addr = memalign(cycle_buffer, BUF_SIZE);
        // Create a memory region for send and receive buffers.
        auto send_buffer_ptr = send_pair->createSendBuffer(i, send_buf_addr, BUF_SIZE);
        auto send_buffer = send_buffer_ptr.release();
        // auto send_buffer = static_cast<gloo::transport::ibverbs::Buffer*>(send_buffer_ptr.release());
        send_buffers.emplace_back(send_buffer);

        auto recv_buffer_ptr = recv_pair->createRecvBuffer(i, recv_buf_addr, BUF_SIZE);
        auto recv_buffer = recv_buffer_ptr.release();
        // auto recv_buffer = static_cast<gloo::transport::ibverbs::Buffer*>(recv_buffer_ptr.release());
        recv_buffers.emplace_back(recv_buffer);
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