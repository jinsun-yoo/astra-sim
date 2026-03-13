#ifndef NCCL_NET_ADAPTER_HH
#define NCCL_NET_ADAPTER_HH

#include <memory>
#include <gloo/transport/context.h>
#include <gloo/transport/buffer.h>

#include "nccl_net_loader.hh"

// Forward-declare NcclGlooBuffer
class NcclGlooBuffer;

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
    NcclNetLoader _loader;

    // Runtime plugin comms created after connect/accept
    void* _listenComm = nullptr;
    void* _sendComm   = nullptr;
    void* _recvComm   = nullptr;
};

// A gloo::transport::Buffer subclass that drives nccl-net isend/irecv/test.
class NcclGlooBuffer : public ::gloo::transport::Buffer {
public:
    // plugin and comm must outlive this buffer.
    // mhandle is the result of regMr for ptr (may be nullptr if regMr failed).
    NcclGlooBuffer(int slot, void* ptr, size_t size, bool is_send,
                   const ncclNet_v10_t* plugin, void* comm, void* mhandle);
    ~NcclGlooBuffer() override;

    void send(size_t offset, size_t length, size_t roffset = 0) override;
    void waitRecv() override;
    void waitSend() override;
    bool pollSend() override;
    bool pollRecv() override;

    // Per-send async interface: each send event owns its own request handle so
    // concurrent sends on the same buffer do not overwrite each other's _request.
    void* beginSendAsync(void* data, size_t length);
    bool  testOwnedSend(void*& request, void* data, size_t length);
    void* dataPtr() const { return ptr_; }

private:
    bool _is_send;
    const ncclNet_v10_t* _plugin;
    void* _comm;
    void* _mhandle;    // memory registration handle from regMr

    void*    _request          = nullptr;
    void*    _send_data        = nullptr;
    size_t   _send_length      = 0;
    uint64_t _null_retry_count = 0;
};

#endif // NCCL_NET_ADAPTER_HH
