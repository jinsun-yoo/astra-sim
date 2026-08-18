/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/system/CommunicatorGroup.hh"

#include <algorithm>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/CollectivePlan.hh"
#include "astra-sim/system/Sys.hh"
#include "spdlog/fmt/ranges.h"

using namespace AstraSim;

CommunicatorGroup::CommunicatorGroup(int id,
                                     std::vector<int> involved_NPUs,
                                     Sys* generator) {
    set_id(id);
    this->involved_NPUs = involved_NPUs;
    this->generator = generator;
    std::sort(involved_NPUs.begin(), involved_NPUs.end());
    assert(std::find(involved_NPUs.begin(), involved_NPUs.end(),
                     generator->id) != involved_NPUs.end());
}

CommunicatorGroup::CommunicatorGroup(int id,
                                     std::vector<int> involved_NPUs,
                                     int rank) {
    set_id(id);
    this->involved_NPUs = involved_NPUs;
    // This is only invoked within Genie's network frontend, to be passed to QPManager, etc. Here, assume we won't need the generator.
    this->generator = nullptr;
    std::sort(involved_NPUs.begin(), involved_NPUs.end());
    assert(std::find(involved_NPUs.begin(), involved_NPUs.end(), rank) != involved_NPUs.end());
}

CommunicatorGroup::~CommunicatorGroup() {
    for (auto cg : comm_plans) {
        CollectivePlan* cp = cg.second;
        delete cp;
    }
}

void CommunicatorGroup::set_id(int id) {
    assert(id >= 0);
    this->id = id;
    this->num_streams = (id + 1) * 1000000;
}

void CommunicatorGroup::set_only_scaleout(bool enabled) {
    only_scaleout = enabled;
}

bool CommunicatorGroup::is_scale_up_domain() const {
    #ifdef TRACE_COMMGROUP
    static auto logger = LoggerFactory::get_logger("commgroup");
    logger->debug("[Is scale-up?] comm_group_id={} with "
                "only_scaleout={}, SCALE_UP_GROUP_SIZE={}",
                id, only_scaleout, SCALE_UP_GROUP_SIZE);
    #endif
    if (involved_NPUs.size() != SCALE_UP_GROUP_SIZE) {
        #ifdef TRACE_COMMGROUP
        logger->debug("  NOT scale-up");
        #endif
        return false;
    }

    // Assumption: All commgroups are at least length 2
    // Assumption: No jumpy rank allocation
    const bool is_contiguous = involved_NPUs.size() >= 2 &&
                               (involved_NPUs[1] - involved_NPUs[0] == 1);
    if (!is_contiguous) {
        #ifdef TRACE_COMMGROUP
        logger->debug("  NOT scale-up");
        #endif
        return false;
    }

    // Why not simply set only_scaleout and use that value throughout? There may be cases where we have both scale-up and out that are each 8 GPUs (64 GPUs in total).
    #ifdef TRACE_COMMGROUP
    if (only_scaleout) {
        logger->debug("  NOT scale-up");
    } else {
        logger->debug("  IS scale-up");
    }
    #endif
    return !only_scaleout;
}

CollectivePlan* CommunicatorGroup::get_collective_plan(ComType comm_type) {
    if (comm_plans.find(comm_type) != comm_plans.end()) {
        return comm_plans[comm_type];
    }

    if (static_cast<uint64_t>(generator->total_nodes) == involved_NPUs.size()) {
        LogicalTopology* logical_topology =
            generator->get_logical_topology(comm_type);
        std::vector<CollectiveImpl*> collective_implementation =
            generator->get_collective_implementation(comm_type);
        std::vector<bool> dimensions_involved(10, true);
        bool should_be_removed = false;
        comm_plans[comm_type] =
            new CollectivePlan(logical_topology, collective_implementation,
                               dimensions_involved, should_be_removed);
        return comm_plans[comm_type];
    } else {
        LogicalTopology* logical_topology = new RingTopology(
            RingTopology::Dimension::Local, generator->id, involved_NPUs);
        std::vector<CollectiveImpl*> collective_implementation{
            new CollectiveImpl(CollectiveImplType::Ring)};
        std::vector<bool> dimensions_involved(1, true);
        bool should_be_removed = true;
        comm_plans[comm_type] =
            new CollectivePlan(logical_topology, collective_implementation,
                               dimensions_involved, should_be_removed);
        return comm_plans[comm_type];
    }
    assert(false);
    return nullptr;
}
