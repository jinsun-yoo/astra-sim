#ifndef QP_POOLER_H
#define QP_POOLER_H

#include <queue>
#include <stdexcept>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <gloo/transport/ibverbs/context.h> 
#include <gloo/common/common.h>

class EventQueue; // Forward declaration

struct CTSEntry {
    int stream_id;
    int qp_idx;
    char padding[56]; // Padding to make the struct size 64 bytes, which is the typical cache line size. This can help avoid false sharing when multiple threads are polling/writing CTS messages for different QPs.
};


// QueuepairManager creates and manages the RDMA QP/memory buffer through Gloo. 
// TODO: For now, we assume a 1-1 relation between RDMA QP and memory buffer.
class QueuepairManager {
public:
    QueuepairManager(std::shared_ptr<gloo::transport::Context> context, std::shared_ptr<spdlog::logger> logger, int rank, int nqps, const std::vector<int>& involved_NPUs, EventQueue* event_queue, int comm_group_id);
    ~QueuepairManager();

    // We implement CTS related behavior (1. Send/recv CTS, 2. Hold send messages until CTS is resolved) here in QPManager, not in Gloo
    // This is because in order to delay the send, we need to leverage the eventqueue (for re-enqueueing, polling, etc)

    // After issuing a recv, send a cts message to peer
    void send_cts_message(int peer_rank, int qp_idx, int stream_id);
    // Insert a ibv_poll_cq to remove the CTS WR from the WRQ. We do not really wait until CTS send is complete. 
    // The assumption is that by the time we call this function, the CTS is already completed, and thus poll should return positive.
    void poll_send_cts_complete(int peer_rank, int qp_idx);
    
    // When sending a message, check if there is a receive slot we can send to.
    int check_incoming_cts(int peer_rank, int qp_idx); 

    // TODO: These buffers are exposed & directly accessible. Move to private, behind fetch_queue.
    // There is a 1-1 relation between buffers and queuepairs.
    std::vector<gloo::transport::Buffer*> send_buffers; // Nranks x NQps
    std::vector<gloo::transport::Buffer*> recv_buffers; // Nranks x NQps
    std::vector<gloo::transport::Buffer*> cts_send_buffers; // Nranks x NQps
    std::vector<gloo::transport::Buffer*> cts_recv_buffers; // Nranks x NQps
    std::vector<int> involved_NPUs; // Ranks this QP Manager communicates with.
    int rank;
    int nranks;
    int nqps;
    int comm_group_id;
private:
    std::shared_ptr<gloo::transport::Context> _context;
    std::shared_ptr<spdlog::logger> _logger;
    std::vector<CTSEntry *> cts_send_ptrs; // Nranks x NQps
    std::vector<CTSEntry *> cts_recv_ptrs; // Nranks x NQps
    std::vector<void*> send_buf_addrs; // Nranks x NQps — raw memaligned data buffers for send
    std::vector<void*> recv_buf_addrs; // Nranks x NQps — raw memaligned data buffers for recv
    std::vector<int> send_ctx_next_idx; // Nranks x NQps
    std::vector<int> recv_ctx_next_idx; // Nranks x NQps
};

#endif // QP_POOLER_H