#include <queue>
#include <stdexcept>
#include <mutex>
#include <string>

#include "qp_pooler.hh"

// Fetch a queue (send or receive)
std::unique_ptr<gloo::transport::Buffer> QueuePairPooler::fetch_queue(bool is_send_queue) {
    std::lock_guard<std::mutex> lock(mutex);
    if (is_send_queue && send_queues.size() == 0 || 
    !is_send_queue && receive_queues.size() == 0) {
        throw std::runtime_error("No send queues available");
    }

    if (is_send_queue) {
        std::unique_ptr<gloo::transport::Buffer> front = std::move(send_queues.front());
        send_queues.pop();
        return std::move(front);
    } else {
        std::unique_ptr<gloo::transport::Buffer> front = std::move(receive_queues.front());
        receive_queues.pop();
        return std::move(front);
    }
}

// Dismiss a queue (send or receive)
void QueuePairPooler::dismiss_queue(std::unique_ptr<gloo::transport::Buffer> buffer, bool is_send_queue) {
    std::lock_guard<std::mutex> lock(mutex);
    if (is_send_queue) {
        send_queues.push(std::move(buffer));
    } else {
        receive_queues.push(std::move(buffer));
    }
}