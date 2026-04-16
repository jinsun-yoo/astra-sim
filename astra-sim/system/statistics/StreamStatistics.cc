#include "StreamStatistics.hh"
#include <iostream>

namespace AstraSim {
void StreamStatistics::update_stats(int id, int elapsed_ns, double collective_size_mb, int num_qps, int num_msgs_per_qp, ComType collective_type, int rank, int num_ranks) {
    this->id = id;
    this->elapsed_ns = elapsed_ns;
    this->collective_size_mb = collective_size_mb;
    this->num_qps = num_qps;
    this->num_msgs_per_qp = num_msgs_per_qp;
    this->num_ranks = num_ranks;
    this->collective_type = collective_type;
    this->rank = rank;
}

void StreamStatistics::postprocess_this_stream() {
    double data_gb = collective_size_mb / 1024.0;
    double busbw_gbs = (elapsed_ns > 0 && num_ranks > 1)
                           ? data_gb / elapsed_ns * 1.0e9 * 2.0 * (num_ranks - 1) / num_ranks
                           : 0;
    if (collective_type != ComType::All_Reduce) {
        busbw_gbs /= 2; // Account for the fact that AllReduce effectively does 3x the data movement of a single send/recv.
    }
    int total_msgs = num_msgs_per_qp * num_qps;
    double msgrate = (elapsed_ns > 0) ? total_msgs * 1.0e9 / elapsed_ns : 0;
    std::cout << "[Rank " << rank << ", Stream " << id << "] type=" << static_cast<int>(collective_type)
              << " elapsed=" << elapsed_ns
              << "ns size=" << collective_size_mb
              << "MB busbw=" << busbw_gbs << " GB/s ("
              << busbw_gbs * 8 << " Gbps)"
              << " msgrate=" << msgrate / 1e6 << " Mpps" << std::endl;
}
}