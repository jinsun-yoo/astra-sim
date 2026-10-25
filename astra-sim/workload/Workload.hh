/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __WORKLOAD_HH__
#define __WORKLOAD_HH__

#include <map>
#include <memory>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>

#include "astra-sim/common/ChromeTracer.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/CommunicatorGroup.hh"
#include "astra-sim/workload/HardwareResource.hh"
#include "extern/graph_frontend/chakra/src/feeder/et_feeder.h"

#define MAX_CHAKRA_NODES 8*1024*1024

namespace AstraSim {

class Sys;
class DataSet;

class Workload : public Callable {
  public:
    Workload(Sys* sys,
             std::string et_filename,
             std::string comm_group_filename,
             ChromeTracer* chrome_tracer);
    ~Workload();

    // communicator groups
    void initialize_comm_group(std::string comm_group_filename);

    // event-based simulation
    void issue_dep_free_nodes();
    void issue(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_replay(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_remote_mem(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_comp(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_comm(std::shared_ptr<Chakra::ETFeederNode> node, bool already_occupied = false);

    // Communication sequencing. Genie runs comm ops of different communicators (and p2p ops to different
    // peers / directions) concurrently, but ops of one communicator domain one at a time in trace (node id)
    // order, like NCCL does per communicator. Key: (canonical group id, p2p peer or -1, p2p is_send or -1).
    using CommSeqKey = std::tuple<int, int, int>;
    struct CommSeqState {
        std::vector<uint64_t> order;
        size_t next = 0;
        bool busy = false;
    };
    void initialize_comm_sequencer();
    bool comm_seq_enqueue(std::shared_ptr<Chakra::ETFeederNode> node);
    void comm_seq_try_dispatch(const CommSeqKey& key);
    void comm_seq_on_complete(uint64_t node_id);
    void comm_seq_reset();
    std::map<CommSeqKey, CommSeqState> comm_seq;
    std::unordered_map<uint64_t, CommSeqKey> comm_seq_key_of;
    std::unordered_map<uint64_t, std::shared_ptr<Chakra::ETFeederNode>> comm_seq_ready;
    bool comm_seq_enabled = false;
    void skip_invalid(std::shared_ptr<Chakra::ETFeederNode> node);
    void call(EventType event, CallData* data);
    void fire();
    void reset_for_next_iteration();
    void finalize_iteration();

    // stats
    void report();

    // Chrometrace
    void chrome_trace_node(std::shared_ptr<Chakra::ETFeederNode> node);
    void chrome_trace_end_node(std::shared_ptr<Chakra::ETFeederNode> node);

    // helpers

    Chakra::ETFeeder* et_feeder;
    std::vector<CommunicatorGroup*> comm_groups;
    HardwareResource* hw_resource;
    Sys* sys;
    std::unordered_map<int, uint64_t> collective_comm_node_id_map;
    std::unordered_map<int, DataSet*> collective_comm_wrapper_map;
    bool is_finished;
    ChromeTracer* chrome_tracer;
    int node_chrometrace_id[MAX_CHAKRA_NODES];
};

}  // namespace AstraSim

#endif /* __WORKLOAD_HH__ */
