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
                void* listenComm = nullptr;
                using listen_fn_t = int (*)(int, void*, void**);
                listen_fn_t listen_fn = reinterpret_cast<listen_fn_t>(plugin->listen);
                int res = listen_fn(0, listen_handle, &listenComm);
                if (res != 0) {
                    std::cerr << "NcclNetAdapter: plugin listen failed: " << res << std::endl;
                    free(listen_handle);
                } else {
                    std::cerr << "NcclNetAdapter: plugin listen succeeded; exchanging handle via MPI" << std::endl;

                    // Exchange listen_handle with right_rank (simple ring test)
                    MPI_Sendrecv_replace(listen_handle, handle_size, MPI_BYTE, right_rank, 0, left_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    // Attempt connect (may require retries per nccl-net semantics)
                    void* remote_handle = listen_handle; // now contains remote peer's handle
                    void* sendComm = nullptr;
                    using connect_fn_t = int (*)(int, void*, void*, void**, void**);
                    connect_fn_t connect_fn = reinterpret_cast<connect_fn_t>(plugin->connect);
                    const int max_tries = 10;
                    for (int t = 0; t < max_tries && !sendComm; ++t) {
                        res = connect_fn(0, nullptr, remote_handle, &sendComm, nullptr);
                        if (res == 0 && sendComm) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (sendComm) {
                        std::cerr << "NcclNetAdapter: plugin connect succeeded after retry" << std::endl;
                        _sendComm = sendComm;
                    } else {
                        std::cerr << "NcclNetAdapter: plugin connect did not produce a sendComm" << std::endl;
                    }

                    // Accept on the listening side (also may require retries)
                    void* recvComm = nullptr;
                    using accept_fn_t = int (*)(void*, void**, void**);
                    accept_fn_t accept_fn = reinterpret_cast<accept_fn_t>(plugin->accept);
                    for (int t = 0; t < max_tries && !recvComm; ++t) {
                        res = accept_fn(listenComm, &recvComm, nullptr);
                        if (res == 0 && recvComm) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (recvComm) {
                        std::cerr << "NcclNetAdapter: plugin accept succeeded after retry" << std::endl;
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
}

// NcclGlooBuffer implementation (stub with regMr sample)
NcclGlooBuffer::NcclGlooBuffer(int slot, void* ptr, size_t size, bool is_send)
    : ::gloo::transport::Buffer(slot, ptr, size), _is_send(is_send), _mhandle(nullptr) {
    std::cerr << "NcclGlooBuffer: created (is_send=" << _is_send << ")" << std::endl;
}

NcclGlooBuffer::~NcclGlooBuffer() {
    std::cerr << "NcclGlooBuffer: destroyed" << std::endl;
}

void NcclGlooBuffer::send(size_t offset, size_t length, size_t roffset) {
    std::cerr << "NcclGlooBuffer::send called offset=" << offset << " length=" << length << std::endl;
    // TODO: use nccl-net isend with _mhandle and remote phandle
}

void NcclGlooBuffer::waitRecv() {
    std::cerr << "NcclGlooBuffer::waitRecv called" << std::endl;
}

void NcclGlooBuffer::waitSend() {
    std::cerr << "NcclGlooBuffer::waitSend called" << std::endl;
}

bool NcclGlooBuffer::pollSend() {
    return false;
}

bool NcclGlooBuffer::pollRecv() {
    return false;
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
    if (_loader.available()) {
        auto buf = new NcclGlooBuffer(slot, ptr, size, true);
        // Only register MR if _sendComm is available
        if (_sendComm) {
            void* m = call_regMr_if_available(_loader, _sendComm, ptr, size, 1 /*NCCL_PTR_HOST*/);
            buf->set_mhandle(m);
            if (!buf->get_mhandle()) {
                std::cerr << "NcclNetAdapter: regMr on sendComm failed; buffer mhandle is null" << std::endl;
            } else {
                std::cerr << "NcclNetAdapter: regMr succeeded on sendComm" << std::endl;
            }
        } else {
            std::cerr << "NcclNetAdapter: sendComm not available; skipping regMr for send buffer" << std::endl;
        }
        return std::unique_ptr<::gloo::transport::Buffer>(buf);
    }
    auto &pair = _context->getPair(_send_id);
    return pair->createSendBuffer(slot, ptr, size);
}

std::unique_ptr<::gloo::transport::Buffer> NcclNetAdapter::createRecvBuffer(int slot, void* ptr, size_t size) {
    if (_loader.available()) {
        auto buf = new NcclGlooBuffer(slot, ptr, size, false);
        if (_recvComm) {
            void* m = call_regMr_if_available(_loader, _recvComm, ptr, size, 1 /*NCCL_PTR_HOST*/);
            buf->set_mhandle(m);
            if (!buf->get_mhandle()) {
                std::cerr << "NcclNetAdapter: regMr on recvComm failed; buffer mhandle is null" << std::endl;
            } else {
                std::cerr << "NcclNetAdapter: regMr succeeded on recvComm" << std::endl;
            }
        } else {
            std::cerr << "NcclNetAdapter: recvComm not available; skipping regMr for recv buffer" << std::endl;
        }
        return std::unique_ptr<::gloo::transport::Buffer>(buf);
    }
    auto &pair = _context->getPair(_recv_id);
    return pair->createRecvBuffer(slot, ptr, size);
}
