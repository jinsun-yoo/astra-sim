// File: qp_pooler.h

#ifndef QP_POOLER_H
#define QP_POOLER_H

#include <queue>
#include <stdexcept>
#include <mutex>
#include <string>
#include <gloo/transport/ibverbs/context.h> // Include the header for gloo::ibverbs::Context

#define BUF_SIZE 65536
#define NUM_BUFS 1024
#define DATA_SIZE 65536*1024
class QueuePairPooler {
private:
    std::queue<std::unique_ptr<gloo::transport::Buffer>> send_queues;
    std::queue<std::unique_ptr<gloo::transport::Buffer>> receive_queues;
    std::mutex mutex; // Mutex to protect access to the queues
    std::shared_ptr<gloo::transport::Context> _context;

public:
    // Constructor
    // QueuePairPooler(std::shared_ptr<gloo::transport::Context> context, int send_id, int recv_id) {
    //     _context = context;
    //     std::unique_ptr<gloo::transport::Buffer>;
    // }    

    std::vector<gloo::transport::Buffer*> send_buffers;
    std::vector<gloo::transport::Buffer*> recv_buffers;

    QueuePairPooler(std::shared_ptr<gloo::transport::Context> context, int send_id, int recv_id) {
        _context = context;
        for (int i =0; i < NUM_BUFS; ++i) {
            char *send_mem = new char[BUF_SIZE]; // Allocate a buffer of size 1024
            char *recv_mem = new char[BUF_SIZE]; // Allocate a buffer of size 1024
            const auto&send_pair = _context->getPair(send_id);
            auto send_buffer = send_pair->createSendBuffer(i, send_mem, BUF_SIZE);
            send_buffers.emplace_back(send_buffer.release());
            const auto&recv_pair = _context->getPair(recv_id);
            auto recv_buffer = recv_pair->createRecvBuffer(i, recv_mem, BUF_SIZE);
            recv_buffers.emplace_back(recv_buffer.release());
        }
    }

    // Fetch a queue (send or receive)
    std::unique_ptr<gloo::transport::Buffer> fetch_queue(bool is_send_queue);

    // Dismiss a queue (send or receive)
    void dismiss_queue(std::unique_ptr<gloo::transport::Buffer> buffer, bool is_send_queue);

    // Fetch a queue (send or receive)
    std::unique_ptr<gloo::transport::Buffer> fetch_buffer(int idx, bool is_send_queue);

    // Dismiss a queue (send or receive)
    void dismiss_buffer(std::unique_ptr<gloo::transport::Buffer> buffer, int idx, bool is_send_queue);
};

#endif // QP_POOLER_H