#include "StatCounter.hh"

namespace AstraSim {
void StatCounter::record_ring_coll(int elapsed_ns, double collective_size_mb, int num_qps, int num_msgs_per_qp, ComType collective_type) {
    StreamStatistics& stat = ring_coll_stats[stat_idx];
    stat.update_stats(stat_idx, elapsed_ns, collective_size_mb, num_qps, num_msgs_per_qp, collective_type, rank, num_ranks);
    stat_idx++;
}

void StatCounter::postprocess_all_streams() {
    for (int i = 0; i < stat_idx; i++) {
        ring_coll_stats[i].postprocess_this_stream();
    }
}
}