#ifndef ASTRA_SIM_STREAM_STATISTICS_HH
#define ASTRA_SIM_STREAM_STATISTICS_HH

#include "astra-sim/system/Common.hh"

namespace AstraSim {
class StreamStatistics {
    public:
        StreamStatistics() {
        }
        void postprocess_this_stream();
        void update_stats(int id, double elapsed_s, double collective_size_mb, int num_qps, int num_msgs_per_qp, ComType collective_type, int rank, int num_ranks);

    private:
        double elapsed_s;
        double collective_size_mb;
        int num_msgs_per_qp;
        int num_qps;
        ComType collective_type;
        int id;
        int rank;
        int num_ranks;
};
}

#endif