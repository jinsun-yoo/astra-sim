#ifndef GENIE_NETWORK_QP_STATS_HH
#define GENIE_NETWORK_QP_STATS_HH

// Low-overhead, env-gated per-QP statistics (GENIE_QP_STATS=1). All hooks are no-ops unless enabled.
// Optional event trace: GENIE_QP_TRACE=<max events>, GENIE_QP_TRACE_SKIP=<events to skip first>,
// written to $GENIE_QP_TRACE_DIR (default $OUTPUT_PATH)/qptrace/rank<r>.csv at exit.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace genie_qpstats {

inline bool env_on(const char* name) {
    const char* s = getenv(name);
    return s && *s && strcmp(s, "0") != 0;
}

inline bool enabled() {
    static const bool e = env_on("GENIE_QP_STATS");
    return e;
}

inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Quarter-octave log histogram of nanosecond values.
struct Hist {
    static constexpr int NB = 256;
    uint64_t b[NB] = {};
    uint64_t n = 0, mx = 0;
    double sum = 0;
    static int idx(uint64_t v) {
        if (v < 4) return (int)v;
        int e = 63 - __builtin_clzll(v);
        int sub = (int)((v >> (e - 2)) & 3);
        return 4 * e + sub - 4;
    }
    static double lower(int i) {
        if (i < 4) return i;
        int e = (i + 4) / 4, sub = (i + 4) % 4;
        return std::ldexp(4.0 + sub, e - 2);
    }
    void add(uint64_t v) {
        b[idx(v)]++;
        n++;
        sum += (double)v;
        if (v > mx) mx = v;
    }
    double pct(double p) const {
        if (n == 0) return 0;
        uint64_t target = (uint64_t)std::ceil(p * n), acc = 0;
        for (int i = 0; i < NB; i++) {
            acc += b[i];
            if (acc >= target) return 0.5 * (lower(i) + lower(i + 1));
        }
        return (double)mx;
    }
    double avg() const { return n ? sum / n : 0; }
    // "avg/p50/p90/p99/max" in microseconds
    std::string us() const {
        char s[160];
        snprintf(s, sizeof(s), "%.1f/%.1f/%.1f/%.1f/%.1f", avg() / 1e3, pct(0.5) / 1e3, pct(0.9) / 1e3,
                 pct(0.99) / 1e3, mx / 1e3);
        return s;
    }
};

struct QpStat {
    bool used = false;
    // send side (data send QP towards peer)
    uint64_t s_msgs = 0, s_bytes = 0;
    uint64_t s_inflight = 0, s_inflight_bytes = 0;
    uint64_t s_inflight_max = 0, s_inflight_bytes_max = 0;
    double s_inflight_sum = 0, s_inflight_bytes_sum = 0;  // sampled after each post
    uint64_t s_post_idle = 0;                              // posts made while QP had nothing in flight
    uint64_t s_idle_ns = 0, s_last_cqe = 0;                // time with 0 in flight before a post
    uint64_t s_cts_first = 0;                              // CTS already present at first check
    uint64_t s_cts_spins = 0;                              // SIM_SEND re-enqueues waiting for CTS
    std::deque<uint64_t> s_q;                              // post timestamps, FIFO (RC completes in order)
    std::deque<uint64_t> s_qb;
    Hist cts_wait;   // sim_send (ready) -> CTS seen & posted
    Hist s_lat;      // post -> send CQE
    Hist s_cqe_gap;  // between consecutive send CQEs
    Hist post_cost;  // duration of the post call
    // recv side (data recv QP from peer)
    uint64_t r_msgs = 0, r_inflight = 0, r_inflight_max = 0, r_last_cqe = 0;
    std::deque<uint64_t> r_q;
    Hist r_lat;      // recv post + CTS send -> recv CQE
    Hist r_cqe_gap;  // between consecutive recv CQEs
};

enum TraceType : uint8_t { T_READY = 0, T_POST = 1, T_SCQE = 2, T_RPOST = 3, T_RCQE = 4 };

struct TraceRec {
    uint64_t t;
    uint32_t seq;
    uint16_t peer;
    uint8_t qp;
    uint8_t type;
};

struct Tracer {
    uint64_t skip = 0, maxn = 0, seen = 0;
    std::vector<TraceRec> recs;
    Tracer() {
        const char* s = getenv("GENIE_QP_TRACE");
        maxn = s ? strtoull(s, nullptr, 10) : 0;
        const char* k = getenv("GENIE_QP_TRACE_SKIP");
        skip = k ? strtoull(k, nullptr, 10) : 0;
        if (maxn) recs.reserve(maxn);
    }
    inline void rec(uint8_t type, int peer, int qp, uint32_t seq, uint64_t t) {
        if (!maxn) return;
        if (seen++ < skip || recs.size() >= maxn) return;
        recs.push_back(TraceRec{t, seq, (uint16_t)peer, (uint8_t)qp, type});
    }
};

inline Tracer& tracer() {
    static Tracer t;
    return t;
}

// Event-loop statistics.
struct LoopStat {
    uint64_t iters = 0, qlen_sum = 0, ns = 0;
    uint64_t poll_send = 0, poll_send_hit = 0, poll_recv = 0, poll_recv_hit = 0;
    uint64_t sim_send = 0, sim_send_miss = 0, sim_recv = 0, sched = 0;
    uint64_t send_cqes = 0, recv_cqes = 0;
    Hist cqes_per_hit;
};

inline LoopStat& loop() {
    static LoopStat l;
    return l;
}

}  // namespace genie_qpstats

#endif  // GENIE_NETWORK_QP_STATS_HH
