#ifndef NCCL_NET_ADAPTER_HH
#define NCCL_NET_ADAPTER_HH

#include <memory>
#include <gloo/transport/context.h>
#include <gloo/transport/buffer.h>

// Lightweight adapter skeleton for nccl-net transport.
// This header defines a minimal NcclNetAdapter that accepts a
// std::shared_ptr<gloo::transport::Context> so it can be constructed
// from QueuepairManager. Implementation currently delegates to the
// underlying gloo Pair buffers as a placeholder.
class NcclNetAdapter {
public:
    NcclNetAdapter(std::shared_ptr<gloo::transport::Context> context, int send_id, int recv_id);
    ~NcclNetAdapter();

    std::unique_ptr<::gloo::transport::Buffer> createSendBuffer(int slot, void* ptr, size_t size);
    std::unique_ptr<::gloo::transport::Buffer> createRecvBuffer(int slot, void* ptr, size_t size);

private:
    std::shared_ptr<gloo::transport::Context> _context;
    int _send_id;
    int _recv_id;
};

#endif // NCCL_NET_ADAPTER_HH
