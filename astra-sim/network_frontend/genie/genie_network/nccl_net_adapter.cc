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
        auto plugin = _loader.plugin();
        if (plugin) {
            // Init — ncclDebugLogger_t is void(*)(int, ulong, const char*, int, const char*, ...)
            if (plugin->init) {
                using ncclDebugLogger_t = void (*)(int, unsigned long, const char*, int, const char*, ...);
                using init_fn_t = int (*)(ncclDebugLogger_t, void*);
                int res = reinterpret_cast<init_fn_t>(plugin->init)(nccl_stub_logger, nullptr);
                std::cerr << "NcclNetAdapter: plugin init returned " << res << std::endl;
            }

            // Query devices
            int ndev = 0;
            if (plugin->devices) {
                using dev_fn_t = int (*)(int*);
                reinterpret_cast<dev_fn_t>(plugin->devices)(&ndev);
                std::cerr << "NcclNetAdapter: plugin reports " << ndev << " devices" << std::endl;
            }

            if (ndev <= 0) {
                std::cerr << "NcclNetAdapter: no devices found; skipping nccl-net comm setup" << std::endl;
            } else if (plugin->listen && plugin->connect && plugin->accept) {
                // Handle buffer must be exactly NCCL_NET_HANDLE_MAXSIZE bytes.
                const size_t handle_size = NCCL_NET_HANDLE_MAXSIZE;
                void* listen_handle = malloc(handle_size);
                using listen_fn_t = int (*)(int, void*, void**);
                int res = reinterpret_cast<listen_fn_t>(plugin->listen)(0, listen_handle, &_listenComm);
                if (res != 0) {
                    std::cerr << "NcclNetAdapter: plugin listen failed: " << res << std::endl;
                    free(listen_handle);
                } else {
                    std::cerr << "NcclNetAdapter: plugin listen succeeded; exchanging handle via MPI" << std::endl;

                    // NCCL semantics: the *receiver* calls listen() and shares its handle
                    // with the sender, who calls connect().  We want:
                    //   sendComm → _send_id  (we connect to _send_id's listen handle)
                    //   recvComm ← _recv_id  (_recv_id connects to our listen handle)
                    // So: send our handle to _recv_id, receive _send_id's handle.
                    MPI_Sendrecv_replace(listen_handle, handle_size, MPI_BYTE,
                                         _recv_id, 0, _send_id, 0,
                                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    void* remote_handle = listen_handle; // now contains _send_id's listen handle
                    void* sendComm = nullptr;
                    void* recvComm = nullptr;
                    using connect_fn_t = int (*)(int, void*, void*, void**, void**);
                    using accept_fn_t  = int (*)(void*, void**, void**);
                    auto connect_fn = reinterpret_cast<connect_fn_t>(plugin->connect);
                    auto accept_fn  = reinterpret_cast<accept_fn_t>(plugin->accept);

                    // connect() and accept() are non-blocking state machines; interleave
                    // retries so both sides make progress concurrently.
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
                        std::cerr << "NcclNetAdapter: sendComm to send_id=" << _send_id << " established" << std::endl;
                        _sendComm = sendComm;
                    } else {
                        std::cerr << "NcclNetAdapter: connect to send_id=" << _send_id << " failed" << std::endl;
                    }
                    if (recvComm) {
                        std::cerr << "NcclNetAdapter: recvComm from recv_id=" << _recv_id << " established" << std::endl;
                        _recvComm = recvComm;
                    } else {
                        std::cerr << "NcclNetAdapter: accept from recv_id=" << _recv_id << " failed" << std::endl;
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
      _is_send(is_send), _plugin(plugin), _comm(comm), _mhandle(mhandle) {
    std::cerr << "NcclGlooBuffer: created (is_send=" << _is_send
              << " size=" << size_
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
    if (_request || _send_data) {
        std::cerr << "NcclGlooBuffer::send: WARNING overlapping send! slot=" << slot_
                  << " _request=" << _request << " _send_data=" << _send_data << std::endl;
    }
    // Save params so pollSend can retry if isend returns NULL request
    _send_data   = static_cast<char*>(ptr_) + offset;
    _send_length = length;
    _null_retry_count = 0;

    using isend_fn_t = int (*)(void*, void*, size_t, int, void*, void*, void**);
    auto isend_fn = reinterpret_cast<isend_fn_t>(_plugin->isend);
    int res = isend_fn(_comm, _send_data, _send_length, /*tag=*/slot_, _mhandle, /*phandle=*/nullptr, &_request);
    if (res != 0)
        std::cerr << "NcclGlooBuffer::send: isend returned error " << res << std::endl;
    else if (_request)
        std::cerr << "NcclGlooBuffer::send: isend immediate success request=" << _request << " slot=" << slot_ << std::endl;
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
        int rc = isend_fn(_comm, _send_data, _send_length, /*tag=*/slot_, _mhandle, /*phandle=*/nullptr, &_request);
        _null_retry_count++;
        if (_request) {
            std::cerr << "NcclGlooBuffer::pollSend: isend posted request=" << _request
                      << " slot=" << slot_ << " after " << _null_retry_count << " retries" << std::endl;
            _null_retry_count = 0;
        } else if (rc != 0) {
            std::cerr << "NcclGlooBuffer::pollSend: isend error rc=" << rc << " slot=" << slot_ << std::endl;
        } else if (_null_retry_count % 10000000 == 0) {
            std::cerr << "NcclGlooBuffer::pollSend: isend null after " << _null_retry_count
                      << " retries, slot=" << slot_ << " send_data=" << _send_data << std::endl;
        }
    }
    if (!_request)
        return false;

    using test_fn_t = int (*)(void*, int*, int*);
    auto test_fn = reinterpret_cast<test_fn_t>(_plugin->test);
    int done = 0, send_size = 0;
    int rc = test_fn(_request, &done, &send_size);
    if (rc != 0) {
        std::cerr << "NcclGlooBuffer::pollSend: test() returned error " << rc
                  << " request=" << _request << std::endl;
        _request   = nullptr;
        _send_data = nullptr;
        return false;
    }
    if (done) {
        std::cerr << "NcclGlooBuffer::pollSend: send done request=" << _request
                  << " size=" << send_size << std::endl;
        _request   = nullptr;
        _send_data = nullptr;
    }
    return done != 0;
}

bool NcclGlooBuffer::pollRecv() {
    if (!_plugin || !_plugin->irecv || !_plugin->test)
        return false;

    if (!_request) {
        void*  data = ptr_;
        size_t sz   = size_;
        int    tag  = slot_;
        void*  mh   = _mhandle;
        void*  ph   = nullptr;
        using irecv_fn_t = int (*)(void*, int, void**, size_t*, int*, void**, void**, void**);
        auto irecv_fn = reinterpret_cast<irecv_fn_t>(_plugin->irecv);
        int res = irecv_fn(_comm, /*n=*/1, &data, &sz, &tag, &mh, &ph, &_request);
        if (res != 0) {
            std::cerr << "NcclGlooBuffer::pollRecv: irecv error " << res << std::endl;
            return false;
        }
        if (!_request) {
            std::cerr << "NcclGlooBuffer::pollRecv: irecv returned null request" << std::endl;
            return false;
        }
        std::cerr << "NcclGlooBuffer::pollRecv: posted irecv req=" << _request << std::endl;
    }

    using test_fn_t = int (*)(void*, int*, int*);
    auto test_fn = reinterpret_cast<test_fn_t>(_plugin->test);
    int done = 0, recv_size = 0;
    int rc = test_fn(_request, &done, &recv_size);
    if (rc != 0) {
        std::cerr << "NcclGlooBuffer::pollRecv: test() error rc=" << rc
                  << " req=" << _request << std::endl;
        _request = nullptr;
        return false;
    }
    if (done) {
        std::cerr << "NcclGlooBuffer::pollRecv: recv done recv_size=" << recv_size << std::endl;
        _request = nullptr;
    }
    return done != 0;
}

void* NcclGlooBuffer::beginSendAsync(void* data, size_t length) {
    if (!_plugin || !_plugin->isend) return nullptr;
    void* request = nullptr;
    using isend_fn_t = int (*)(void*, void*, size_t, int, void*, void*, void**);
    auto isend_fn = reinterpret_cast<isend_fn_t>(_plugin->isend);
    int rc = isend_fn(_comm, data, length, /*tag=*/slot_, _mhandle, /*phandle=*/nullptr, &request);
    if (rc != 0)
        std::cerr << "NcclGlooBuffer::beginSendAsync: isend error rc=" << rc << std::endl;
    else if (request)
        std::cerr << "NcclGlooBuffer::beginSendAsync: posted request=" << request
                  << " slot=" << slot_ << " len=" << length << std::endl;
    return request;
}

bool NcclGlooBuffer::testOwnedSend(void*& request, void* data, size_t length) {
    if (!_plugin || !_plugin->test) return false;
    if (!request) {
        if (!data) return false;
        request = beginSendAsync(data, length);
        if (!request) return false;
    }
    using test_fn_t = int (*)(void*, int*, int*);
    auto test_fn = reinterpret_cast<test_fn_t>(_plugin->test);
    int done = 0, sz = 0;
    int rc = test_fn(request, &done, &sz);
    if (rc != 0) {
        std::cerr << "NcclGlooBuffer::testOwnedSend: test() error rc=" << rc
                  << " request=" << request << std::endl;
        request = nullptr;
        return false;
    }
    if (done) {
        std::cerr << "NcclGlooBuffer::testOwnedSend: done request=" << request
                  << " sz=" << sz << std::endl;
        request = nullptr;
    }
    return done != 0;
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
