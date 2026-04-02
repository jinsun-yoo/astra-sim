#ifndef QP_POOLER_H
#define QP_POOLER_H

#include <queue>
#include <stdexcept>
#include <mutex>
#include <string>

#include <spdlog/spdlog.h>
#include <gloo/transport/ibverbs/context.h> 
#include <gloo/common/common.h>

struct CTSEntry {
    int stream_id;
    int qp_idx;
    char padding[56]; // Padding to make the struct size 64 bytes, which is the typical cache line size. This can help avoid false sharing when multiple threads are polling/writing CTS messages for different QPs.
};


// QueuepairManager creates and manages the RDMA QP/memory buffer through Gloo. 
// TODO: For now, we assume a 1-1 relation between RDMA QP and memory buffer.
class QueuepairManager {
public:
    QueuepairManager(std::shared_ptr<gloo::transport::Context> context, std::shared_ptr<spdlog::logger> logger, int send_id, int recv_id, int nqps); 

    // TODO: For now, ASTRASimGenieNetwork directly access the buffers, instead of calling fetch_buffer.
    // Fetches the required buffer.  
    // If not ready, wait until ready.
    std::unique_ptr<gloo::transport::Buffer> fetch_buffer(int idx, bool is_send_queue);

    // Marks the buffer ready for another operation.
    void dismiss_buffer(std::unique_ptr<gloo::transport::Buffer> buffer, int idx, bool is_send_queue);

    // Gloo exposes QueuePairs as <gloo::transport::Pair> and MR as <gloo::transport::Buffer>. 
    // Gloo exposes send/receive primitives as member functions of gloo::transport::Buffer
    // i.e.) buffer->send(), buffer->recv(), etc.
    // Therefore it is highly unlikely that we will need to use the RDMA QP directly.
    // TODO: Consider renaming this from 'QueuepairManager'.
    std::unique_ptr<gloo::transport::Buffer> fetch_queue(bool is_send_queue);

    void dismiss_queue(std::unique_ptr<gloo::transport::Buffer> buffer, bool is_send_queue);
    void send_cts_message(int qp_idx, int stream_id);
    int check_incoming_cts(int qp_idx); 
    void poll_send_cts_complete(int qp_idx);

    // TODO: These buffers are exposed & directly accessible. Move to private, behind fetch_queue.
    // TODO: For now, we separate buffers (and queuepairs) for send & receives. In the long term, merge them into one pool. 
    std::vector<gloo::transport::Buffer*> send_buffers;
    std::vector<gloo::transport::Buffer*> recv_buffers;
    std::vector<gloo::transport::Buffer*> cts_send_buffers;
    std::vector<gloo::transport::Buffer*> cts_recv_buffers;
private:
    std::queue<std::unique_ptr<gloo::transport::Buffer>> send_queues;
    std::queue<std::unique_ptr<gloo::transport::Buffer>> receive_queues;
    std::mutex _mutex; // Mutex to protect access to the queues
    std::shared_ptr<gloo::transport::Context> _context;
    std::shared_ptr<spdlog::logger> _logger;
    std::vector<CTSEntry *> cts_send_ptrs;
    std::vector<CTSEntry *> cts_recv_ptrs;
    std::vector<int> send_ctx_next_idx;
    std::vector<int> recv_ctx_next_idx;
};

#endif // QP_POOLER_H