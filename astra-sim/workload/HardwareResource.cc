/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

// TODO: HardwareResource.cc should be moved to the system layer.

#include "astra-sim/workload/HardwareResource.hh"

using namespace std;
using namespace AstraSim;
using namespace Chakra;

typedef ChakraProtoMsg::NodeType ChakraNodeType;

HardwareResource::HardwareResource(uint32_t num_npus)
    : num_npus(num_npus){
    //   num_in_flight_cpu_ops(0),
    //   num_in_flight_gpu_comm_ops(0),
    //   num_in_flight_gpu_comp_ops(0) {

    // num_cpu_ops = 0;
    // num_gpu_ops = 0;
    // num_gpu_comms = 0;

    // tics_cpu_ops = 0;
    // tics_gpu_ops = 0;
    // tics_gpu_comms = 0;

    // cpu_ops_node = NULL;
    // gpu_ops_node = NULL;
    // gpu_comms_node = NULL;
}

void HardwareResource::initialize_queues(
    const unordered_set<Chakra::DepQueue>& queue_ids) {
    for (const auto queue_id : queue_ids) {
        num_in_flight_ops.try_emplace(queue_id, 0);
        num_ops.try_emplace(queue_id, 0);
        tics_ops.try_emplace(queue_id, 0);
    }
}

void HardwareResource::occupy(const shared_ptr<Chakra::ETFeederNode> node) {
    assert(num_in_flight_ops.find(node->tid()) != num_in_flight_ops.end());
    assert(num_in_flight_ops.at(node->tid()) == 0);
    num_in_flight_ops.at(node->tid()) = 1;
    assert(num_ops.find(node->tid()) != num_ops.end());
    ++num_ops.at(node->tid());

    // if (node->is_cpu_op()) {
    //     assert(num_in_flight_cpu_ops == 0);
    //     ++num_in_flight_cpu_ops;
    //     ++num_cpu_ops;
    // } else {
    //     // if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
    //     //     return;
    //     // } else {
    //         // We treat GPU COMP op and GPU COMM op in one bucket
    //         assert(num_in_flight_gpu_comp_ops == 0);
    //         ++num_in_flight_gpu_comp_ops;
    //         ++num_gpu_ops;
    //         gpu_ops_node = node;
    //     // }
    // }
}

void HardwareResource::release(const shared_ptr<Chakra::ETFeederNode> node) {
    assert(num_in_flight_ops.find(node->tid()) != num_in_flight_ops.end());
    assert(num_in_flight_ops.at(node->tid()) == 1);
    num_in_flight_ops.at(node->tid()) = 0;
    // if (node->is_cpu_op()) {
    //     --num_in_flight_cpu_ops;
    //     assert(num_in_flight_cpu_ops == 0);
    // } else {
    //     // if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
    //     //     return;
    //     // } else {
    //     // Combine GPU COMM and GPU COMP into one bucket
    //     --num_in_flight_gpu_comp_ops;
    //     assert(num_in_flight_gpu_comp_ops == 0);
    //     // }
    // }
}

bool HardwareResource::is_available(
    const shared_ptr<Chakra::ETFeederNode> node) const {
    assert(num_in_flight_ops.find(node->tid()) != num_in_flight_ops.end());
    return num_in_flight_ops.at(node->tid()) == 0;
    // if (node->is_cpu_op()) {
    //     if (num_in_flight_cpu_ops == 0) {
    //         return true;
    //     } else {
    //         return false;
    //     }
    // } else {
    //     // P2P send and recv should not be triggered at the same time, for Genie. 
    //     // Again, best way is to split by stream id. 
    //     // if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
    //     //     return true;
    //     // } else {
    //     // Combine GPU COMM and GPU COMP into one bucket
    //     if (num_in_flight_gpu_comp_ops == 0) {
    //         return true;
    //     } else {
    //         return false;
    //     }
    //     // }
    // }
}

bool HardwareResource::is_idle() const {
    bool is_idle = true;
    for (const auto& pair : num_in_flight_ops) {
        if (pair.second != 0) {
            is_idle = false;
            break;
        }
    }
    return is_idle;
    // return num_in_flight_cpu_ops == 0 &&
    //        num_in_flight_gpu_comp_ops == 0 &&
    //        num_in_flight_gpu_comm_ops == 0;
}

void HardwareResource::reset_inflight_counts() {
    for (auto& pair : num_in_flight_ops) {
        pair.second = 0;
    }
    for (auto& pair : num_ops) {
        pair.second = 0;
    }
    // num_in_flight_cpu_ops = 0;
    // num_in_flight_gpu_comp_ops = 0;
    // num_in_flight_gpu_comm_ops = 0;
    // cpu_ops_node = nullptr;
    // gpu_ops_node = nullptr;
    // gpu_comms_node = nullptr;
}

void HardwareResource::report() {
    // cout << "num_cpu_ops: " << num_cpu_ops << endl;
    // cout << "num_gpu_ops: " << num_gpu_ops << endl;
    // cout << "num_gpu_comms: " << num_gpu_comms << endl;

    // cout << "tics_cpu_ops: " << tics_cpu_ops << endl;
    // cout << "tics_gpu_ops: " << tics_gpu_ops << endl;
    // cout << "tics_gpu_comms: " << tics_gpu_comms << endl;
}
