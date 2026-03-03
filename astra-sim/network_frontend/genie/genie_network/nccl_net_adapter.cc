#include "nccl_net_adapter.hh"
#include <iostream>

NcclNetAdapter::NcclNetAdapter(std::shared_ptr<gloo::transport::Context> context, int send_id, int recv_id)
    : _context(context), _send_id(send_id), _recv_id(recv_id) {
    std::cerr << "NcclNetAdapter: placeholder initialized" << std::endl;
}

NcclNetAdapter::~NcclNetAdapter() {
    std::cerr << "NcclNetAdapter: placeholder destroyed" << std::endl;
}

std::unique_ptr<::gloo::transport::Buffer> NcclNetAdapter::createSendBuffer(int slot, void* ptr, size_t size) {
    auto &pair = _context->getPair(_send_id);
    return pair->createSendBuffer(slot, ptr, size);
}

std::unique_ptr<::gloo::transport::Buffer> NcclNetAdapter::createRecvBuffer(int slot, void* ptr, size_t size) {
    auto &pair = _context->getPair(_recv_id);
    return pair->createRecvBuffer(slot, ptr, size);
}
