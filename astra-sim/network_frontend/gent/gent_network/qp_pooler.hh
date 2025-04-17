// File: qp_pooler.h

#ifndef QP_POOLER_H
#define QP_POOLER_H

#include <queue>
#include <stdexcept>
#include <mutex>
#include <string>
#include <gloo/transport/ibverbs/context.h> // Include the header for gloo::ibverbs::Context

class QueuePairPooler {
private:
    std::queue<std::unique_ptr<gloo::transport::Buffer>> send_queues;
    std::queue<std::unique_ptr<gloo::transport::Buffer>> receive_queues;
    std::mutex mutex; // Mutex to protect access to the queues
    std::shared_ptr<gloo::transport::Context> _context;

public:
    // Constructor
    QueuePairPooler(std::shared_ptr<gloo::transport::Context> context, int send_id, int recv_id) {
        _context = context;
        //auto& send = _context->createPair(send_id);
        //auto& recv = _context->createPair(recv_id);
        for (int i = 0; i < 8; ++i) {
            int* send_mem = new int[16777216]; // Allocate a buffer of size 1024
            int* recv_mem = new int[16777216]; // Allocate a buffer of size 1024
            const auto&send_pair = _context->getPair(send_id);
            auto send_buffer = send_pair->createSendBuffer(i, send_mem, 67108864);
            send_queues.push(std::move(send_buffer));
            const auto&recv_pair = _context->getPair(recv_id);
            auto recv_buffer = recv_pair->createRecvBuffer(i, recv_mem, 67108864);
            receive_queues.push(std::move(recv_buffer));
        }
    }

    // Fetch a queue (send or receive)
    std::unique_ptr<gloo::transport::Buffer> fetch_queue(bool is_send_queue);

    // Dismiss a queue (send or receive)
    void dismiss_queue(std::unique_ptr<gloo::transport::Buffer> buffer, bool is_send_queue);
};

#endif // QP_POOLER_H