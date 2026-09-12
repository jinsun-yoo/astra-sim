#ifndef STAT_COUNTER_HH
#define STAT_COUNTER_HH

#include <vector>

#include "StreamStatistics.hh"

namespace AstraSim {

class StatCounter {
  public:
    StatCounter(int rank, int num_ranks) : rank(rank), num_ranks(num_ranks) {};
    void record_ring_coll(int elapsed_ns, double collective_size_mb, int num_qps, int num_msgs_per_qp, ComType collective_type);
    void postprocess_all_streams();

  private:
    int stat_idx = 0;
    // Was previously a fixed-size C array (StreamStatistics ring_coll_stats[1000]), sized for a single
    // replay. stat_idx is never reset between --num_iterations replays (main.cc's per-iteration loop
    // only resets Workload/EventQueue state), so on any workload with >1000 total ring-collective
    // completions across all iterations combined, indexing past the fixed bound silently corrupted the
    // heap (observed via valgrind as an "Invalid write ... N bytes after a block of size 40,016 alloc'd"
    // in StreamStatistics::update_stats), causing an unrelated double-free/heap-corruption abort much
    // later. A std::vector removes the bound entirely; growth cost here is negligible since this is once
    // per completed collective, not per event-loop tick.
    std::vector<StreamStatistics> ring_coll_stats;
    int rank;
    int num_ranks;
}; 
}

#endif