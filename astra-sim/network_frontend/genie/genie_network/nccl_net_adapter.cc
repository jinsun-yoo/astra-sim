#include "nccl_net_adapter.hh"
#include "nccl_net_plugin_v10.hh"
#include "nccl_net_loader.hh"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdarg>

static void nccl_stub_logger(int /*level*/, unsigned long /*flags*/,
                              const char* /*file*/, int /*line*/,
                              const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

#ifdef GLOO_USE_MPI
#include <mpi.h>
#endif

NcclNetAdapter::NcclNetAdapter(std::shared_ptr<gloo::transport::Context> context, int send_id, int recv_id)
    : _context(context), _send_id(send_id), _recv_id(recv_id), _loader() {
    if (_loader.available()) {
        std::cerr << "NcclNetAdapter: nccl-net plugin available, adapter will use plugin when implemented." << std::endl;
    } else {
        std::cerr << "NcclNetAdapter: nccl-net plugin not available; falling back to Gloo pairs." << std::endl;
    }

    // Attempt a simple device/comm setup if plugin present and MPI is enabled
    if (_loader.available()) {
#ifdef GLOO_USE_MPI
        int rank = 0, world = 1;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world);
        int right_rank = (rank + 1) % world;
        int left_rank = (rank - 1 + world) % world;

        auto plugin = _loader.plugin();
        if (plugin) {
            // Call init if available (safe no-op if null function pointers)
            if (plugin->init) {
                using init_fn_t = int (*)(void*, void*);
                init_fn_t init_fn = reinterpret_cast<init_fn_t>(plugin->init);
                if (init_fn) {
                    int res = init_fn((void*)nccl_stub_logger, nullptr);
                    std::cerr << "NcclNetAdapter: plugin init returned " << res << std::endl;
                }
            }

            // Query devices
            int ndev = 0;
            if (plugin->devices) {
                using dev_fn_t = int (*)(int*);
                dev_fn_t dev_fn = reinterpret_cast<dev_fn_t>(plugin->devices);
                dev_fn(&ndev);
                std::cerr << "NcclNetAdapter: plugin reports " << ndev << " devices" << std::endl;
            }

            if (ndev <= 0) {
                std::cerr << "NcclNetAdapter: no devices found; skipping nccl-net comm setup" << std::endl;
            } else if (plugin->listen && plugin->connect && plugin->accept) {
                // Use a modest handle size that most plugins expect
                const size_t handle_size = 256;
                void* listen_handle = malloc(handle_size);
                using listen_fn_t = int (*)(int, void*, void**);
                listen_fn_t listen_fn = reinterpret_cast<listen_fn_t>(plugin->listen);
                int res = listen_fn(0, listen_handle, &_listenComm);
                if (res != 0) {
                    std::cerr << "NcclNetAdapter: plugin listen failed: " << res << std::endl;
                    free(listen_handle);
                } else {
                    std::cerr << "NcclNetAdapter: plugin listen succeeded; exchanging handle via MPI" << std::endl;

                    // Exchange listen_handle with right_rank (simple ring test)
                    MPI_Sendrecv_replace(listen_handle, handle_size, MPI_BYTE, right_rank, 0, left_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    // Interleave connect and accept retries so both sides make
                    // progress concurrently — connect needs the remote accept and
                    // vice versa, so running them sequentially deadlocks.
                    void* remote_handle = listen_handle; // now contains remote peer's handle
                    void* sendComm = nullptr;
                    void* recvComm = nullptr;
                    using connect_fn_t = int (*)(int, void*, void*, void**, void**);
                    using accept_fn_t  = int (*)(void*, void**, void**);
                    connect_fn_t connect_fn = reinterpret_cast<connect_fn_t>(plugin->connect);
                    accept_fn_t  accept_fn  = reinterpret_cast<accept_fn_t>(plugin->accept);
                    const int max_tries = 100;
                    for (int t = 0; t < max_tries && (!sendComm || !recvComm); ++t) {
                        if (!sendComm)
                            connect_fn(0, nullptr, remote_handle, &sendComm, nullptr);
                        if (!recvComm)
                            accept_fn(_listenComm, &recvComm, nullptr);
                        if (!sendComm || !recvComm)
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (sendComm) {
                        std::cerr << "NcclNetAdapter: plugin connect succeeded" << std::endl;
                        _sendComm = sendComm;
                    } else {
                        std::cerr << "NcclNetAdapter: plugin connect did not produce a sendComm" << std::endl;
                    }
                    if (recvComm) {
                        std::cerr << "NcclNetAdapter: plugin accept succeeded" << std::endl;
                        _recvComm = recvComm;
                    } else {
                        std::cerr << "NcclNetAdapter: plugin accept did not produce a recvComm" << std::endl;
                    }

                    free(listen_handle);
                }
            } else {
                std::cerr << "NcclNetAdapter: plugin missing connect/accept/listen symbols" << std::endl;
            }
        } else {
            std::cerr << "NcclNetAdapter: loader.plugin() returned null" << std::endl;
        }
#else
        std::cerr << "NcclNetAdapter: plugin present but MPI not enabled; skipping comm setup" << std::endl;
#endif
    }
}

NcclNetAdapter::~NcclNetAdapter() {
    std::cerr << "NcclNetAdapter::~NcclNetAdapter: enter" << std::endl;
    auto plugin = _loader.plugin();
    if (!plugin) { std::cerr << "NcclNetAdapter::~NcclNetAdapter: no plugin, done" << std::endl; return; }
    if (_sendComm && plugin->closeSend) {
        std::cerr << "NcclNetAdapter::~NcclNetAdapter: calling closeSend" << std::endl;
        using fn_t = int (*)(void*);
        reinterpret_cast<fn_t>(plugin->closeSend)(_sendComm);
        _sendComm = nullptr;
        std::cerr << "NcclNetAdapter::~NcclNetAdapter: closeSend done" << std::endl;
    }
    if (_recvComm && plugin->closeRecv) {
        std::cerr << "NcclNetAdapter::~NcclNetAdapter: calling closeRecv" << std::endl;
        using fn_t = int (*)(void*);
        reinterpret_cast<fn_t>(plugin->closeRecv)(_recvComm);
        _recvComm = nullptr;
        std::cerr << "NcclNetAdapter::~NcclNetAdapter: closeRecv done" << std::endl;
    }
    if (_listenComm && plugin->closeListen) {
        std::cerr << "NcclNetAdapter::~NcclNetAdapter: calling closeListen" << std::endl;
        using fn_t = int (*)(void*);
        reinterpret_cast<fn_t>(plugin->closeListen)(_listenComm);
        _listenComm = nullptr;
        std::cerr << "NcclNetAdapter::~NcclNetAdapter: closeListen done" << std::endl;
    }
    std::cerr << "NcclNetAdapter::~NcclNetAdapter: exit" << std::endl;
}

NcclGlooBuffer::NcclGlooBuffer(int slot, void* ptr, size_t size, bool is_send,
                               const ncclNet_v10_t* plugin, void* comm, void* mhandle)
    : ::gloo::transport::Buffer(slot, ptr, size),
      _is_send(is_send), _plugin(plugin), _comm(comm), _mhandle(mhandle), _request(nullptr) {
    std::cerr << "NcclGlooBuffer: created (is_send=" << _is_send
              << " comm=" << comm << " mhandle=" << mhandle << ")" << std::endl;
}

NcclGlooBuffer::~NcclGlooBuffer() {
    std::cerr << "NcclGlooBuffer::~NcclGlooBuffer: enter (is_send=" << _is_send << " mhandle=" << _mhandle << ")" << std::endl;
    if (_mhandle && _plugin && _plugin->deregMr) {
        std::cerr << "NcclGlooBuffer::~NcclGlooBuffer: calling deregMr" << std::endl;
        using deregmr_fn_t = int (*)(void*, void*);
        reinterpret_cast<deregmr_fn_t>(_plugin->deregMr)(_comm, _mhandle);
        _mhandle = nullptr;
        std::cerr << "NcclGlooBuffer::~NcclGlooBuffer: deregMr done" << std::endl;
    }
    std::cerr << "NcclGlooBuffer::~NcclGlooBuffer: exit" << std::endl;
}

void NcclGlooBuffer::send(size_t offset, size_t length, size_t /*roffset*/) {
    if (!_plugin || !_plugin->isend) {
        std::cerr << "NcclGlooBuffer::send: plugin or isend missing" << std::endl;
        return;
    }
    // Save params so pollSend can retry if isend returns NULL request
    _send_data   = static_cast<char*>(ptr_) + offset;
    _send_length = length;

    using isend_fn_t = int (*)(void*, void*, size_t, int, void*, void*, void**);
    auto isend_fn = reinterpret_cast<isend_fn_t>(_plugin->isend);
    int res = isend_fn(_comm, _send_data, _send_length, /*tag=*/0, _mhandle, /*phandle=*/nullptr, &_request);
    if (res != 0)
        std::cerr << "NcclGlooBuffer::send: isend returned error " << res << std::endl;
}

void NcclGlooBuffer::waitSend() {
    while (!pollSend()) {}
}

void NcclGlooBuffer::waitRecv() {
    while (!pollRecv()) {}
}

bool NcclGlooBuffer::pollSend() {
    if (!_plugin || !_plugin->isend || !_plugin->test)
        return false;

    // isend may return NULL request if it cannot be posted yet — retry
    if (!_request && _send_data) {
        using isend_fn_t = int (*)(void*, void*, size_t, int, void*, void*, void**);
        auto isend_fn = reinterpret_cast<isend_fn_t>(_plugin->isend);
        isend_fn(_comm, _send_data, _send_length, /*tag=*/0, _mhandle, /*phandle=*/nullptr, &_request);
    }
    if (!_request)
        return false;

    using test_fn_t = int (*)(void*, int*, int*);
    auto test_fn = reinterpret_cast<test_fn_t>(_plugin->test);
    int done = 0;
    test_fn(_request, &done, /*sizes=*/nullptr);
    if (done) {
        _request   = nullptr;
        _send_data = nullptr;  // clear so we don't retry a completed send
    }
    return done != 0;
}

bool NcclGlooBuffer::pollRecv() {
    if (!_plugin || !_plugin->irecv || !_plugin->test)
        return false;

    using test_fn_t = int (*)(void*, int*, int*);
    auto test_fn = reinterpret_cast<test_fn_t>(_plugin->test);

    // If iflush was posted, poll it — once done the sender's test() will complete
    if (_flush_request) {
        int done = 0;
        test_fn(_flush_request, &done, nullptr);
        if (done) _flush_request = nullptr;
        return done != 0;
    }

    // Lazily post irecv on first poll
    if (!_request) {
        void*  data[1]     = { ptr_ };
        size_t sizes[1]    = { size_ };
        int    tags[1]     = { 0 };
        void*  mhandles[1] = { _mhandle };
        void*  phandles[1] = { nullptr };
        using irecv_fn_t = int (*)(void*, int, void**, size_t*, int*, void**, void**, void**);
        auto irecv_fn = reinterpret_cast<irecv_fn_t>(_plugin->irecv);
        int res = irecv_fn(_comm, /*n=*/1, data, sizes, tags, mhandles, phandles, &_request);
        if (res != 0 || !_request) {
            std::cerr << "NcclGlooBuffer::pollRecv: irecv returned error " << res << std::endl;
            return false;
        }
    }

    // Poll irecv
    int done = 0;
    int recv_size = 0;
    test_fn(_request, &done, &recv_size);
    if (!done)
        return false;
    _request = nullptr;

    // irecv done — post iflush to ACK the sender (required for RDMA send completion)
    if (_plugin->iflush) {
        void*  data[1]     = { ptr_ };
        int    sizes[1]    = { recv_size };
        void*  mhandles[1] = { _mhandle };
        using iflush_fn_t = int (*)(void*, int, void**, int*, void**, void**);
        auto iflush_fn = reinterpret_cast<iflush_fn_t>(_plugin->iflush);
        int res = iflush_fn(_comm, /*n=*/1, data, sizes, mhandles, &_flush_request);
        if (res != 0 || !_flush_request) {
            std::cerr << "NcclGlooBuffer::pollRecv: iflush returned error " << res << "; skipping flush" << std::endl;
            return true;
        }
        // Poll flush immediately; if not done yet, return false to re-enqueue
        done = 0;
        test_fn(_flush_request, &done, nullptr);
        if (done) _flush_request = nullptr;
        return done != 0;
    }

    return true;
}

static void* call_regMr_if_available(const NcclNetLoader &loader, void* comm, void* data, size_t size, int type) {
    auto plugin = loader.plugin();
    if (!plugin || !plugin->regMr) return nullptr;
    using regmr_fn_t = int (*)(void*, void*, size_t, int, void**);
    regmr_fn_t regmr = reinterpret_cast<regmr_fn_t>(plugin->regMr);
    void* mhandle = nullptr;
    int res = regmr(comm, data, size, type, &mhandle);
    if (res != 0) {
        std::cerr << "regMr returned error " << res << std::endl;
        return nullptr;
    }
    return mhandle;
}

std::unique_ptr<::gloo::transport::Buffer> NcclNetAdapter::createSendBuffer(int slot, void* ptr, size_t size) {
    auto plugin = _loader.plugin();
    if (plugin && _sendComm) {
        void* mhandle = call_regMr_if_available(_loader, _sendComm, ptr, size, 1 /*NCCL_PTR_HOST*/);
        if (!mhandle)
            std::cerr << "NcclNetAdapter: regMr on sendComm failed; buffer mhandle is null" << std::endl;
        return std::unique_ptr<::gloo::transport::Buffer>(
            new NcclGlooBuffer(slot, ptr, size, true, plugin, _sendComm, mhandle));
    }
    std::cerr << "NcclNetAdapter: createSendBuffer slot=" << slot
              << " falling back to Gloo pair (plugin=" << plugin
              << " sendComm=" << _sendComm << ")" << std::endl;
    auto &pair = _context->getPair(_send_id);
    return pair->createSendBuffer(slot, ptr, size);
}

std::unique_ptr<::gloo::transport::Buffer> NcclNetAdapter::createRecvBuffer(int slot, void* ptr, size_t size) {
    auto plugin = _loader.plugin();
    if (plugin && _recvComm) {
        void* mhandle = call_regMr_if_available(_loader, _recvComm, ptr, size, 1 /*NCCL_PTR_HOST*/);
        if (!mhandle)
            std::cerr << "NcclNetAdapter: regMr on recvComm failed; buffer mhandle is null" << std::endl;
        return std::unique_ptr<::gloo::transport::Buffer>(
            new NcclGlooBuffer(slot, ptr, size, false, plugin, _recvComm, mhandle));
    }
    std::cerr << "NcclNetAdapter: createRecvBuffer slot=" << slot
              << " falling back to Gloo pair (plugin=" << plugin
              << " recvComm=" << _recvComm << ")" << std::endl;
    auto &pair = _context->getPair(_recv_id);
    return pair->createRecvBuffer(slot, ptr, size);
}
