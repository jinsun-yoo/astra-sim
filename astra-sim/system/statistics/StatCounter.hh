#ifndef STAT_COUNTER_HH
#define STAT_COUNTER_HH

#include "StreamStatistics.hh"

namespace AstraSim {

class StatCounter {
  public:
    StatCounter(int rank, int num_ranks) : rank(rank), num_ranks(num_ranks) {};
    void record_ring_coll(int elapsed_ns, double collective_size_mb, int num_qps, int num_msgs_per_qp, ComType collective_type);
    void postprocess_all_streams();

  private:
    int stat_idx = 0;
    StreamStatistics ring_coll_stats[1000]; // arbitrary size, can be expanded if needed.
    int rank;
    int num_ranks;
}; 
}

#endif