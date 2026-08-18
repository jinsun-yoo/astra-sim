#include "StreamStatistics.hh"
#include "astra-sim/common/Logging.hh"

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
                           ? data_gb / elapsed_ns * 1.0e9
                           : 0;

    // Match NCCL bus-bandwidth semantics:
    // - AllGather: baseBw = (count * typesize * nranks) / sec; busBw = baseBw * (nranks - 1) / nranks
    // - AllReduce: baseBw = (count * typesize) / sec; busBw = baseBw * 2 * (nranks - 1) / nranks
    // In the simulator, `collective_size_mb` is the per-rank payload size, so the same effective
    // scaling is applied directly to the per-rank throughput before multiplying by the collective factor.
    if (collective_type == ComType::All_Reduce) {
        busbw_gbs *= 2.0 * (num_ranks - 1) / num_ranks;
    } else {
        busbw_gbs *= (num_ranks - 1) / static_cast<double>(num_ranks);
    }

    int total_msgs = num_msgs_per_qp * num_qps;
    double msgrate = (elapsed_ns > 0) ? total_msgs * 1.0e9 / elapsed_ns : 0;
    AstraSim::LoggerFactory::get_logger("system::statistics::StreamStatistics")
        ->info("[Rank {}, Stream {}] type={} elapsed={}ns size={}MB busbw={} GB/s ({} Gbps) msgrate={} Mpps",
               rank,
               id,
               static_cast<int>(collective_type),
               elapsed_ns,
               collective_size_mb,
               busbw_gbs,
               busbw_gbs * 8,
               msgrate / 1e6);
}
}