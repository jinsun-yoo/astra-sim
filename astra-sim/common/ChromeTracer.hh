#ifndef CHROME_TRACER_HH
#define CHROME_TRACER_HH

#include <string>
#include <cstdint>
#include <fstream>
constexpr size_t MAX_QUEUE_SIZE =  256 * 1024;

namespace AstraSim {

enum ChromeEventType {
    UNKNOWN = 0,
    WORKLOAD_CPU = 1,
    WORKLOAD_GPU_COMP = 2,
    WORKLOAD_GPU_COMM = 3,
};

class ChromeEvent {
    public:
        ChromeEvent(); // Default constructor
        ChromeEvent(const std::string& name, const std::string& category, const std::string& phase, uint64_t timestamp);

        std::string toJson() const;
        void postprocess();

        std::string name;
        std::string category;
        std::string phase = "X";
        float start_ts_micro;
        size_t end_hw_ctr;
        float duration_micro;
        long double start_time_nano;
        long double end_time_nano;
        int event_type;
        bool did_sleep;
        int rank;
};

struct LogPollEvent {
    uint64_t logstartdur;
    uint64_t polldur;
    uint64_t logcompleteenddur;
    uint64_t msghandlerdur;
    uint64_t eventconstrdur;
    uint64_t addpolldur;
    uint64_t logenddur;
};

class ChromeTracer {
    public:
        ChromeTracer();
        ~ChromeTracer();

        void startTrace(const std::string& traceFile);
        void stopTrace();
        int logEventStart(const std::string& name, const std::string& category, ChromeEventType event_type, long double start_timestamp_nano, int rank);
        void logEventEnd(size_t entry_idx, long double end_timestamp_nano);

    private:
        void get_and_setfilename();
        void set_cpu_freq();
        std::ofstream wait_and_get_logfile(bool is_pollrecv_log = false);
        void close_and_signal_ofs(std::ofstream& ofs);

        std::string log_filename;
        std::string log_poll_filename;
        ChromeEvent entry_queue[MAX_QUEUE_SIZE]; // Fixed size for simplicity
        LogPollEvent logpoll_queue[MAX_QUEUE_SIZE]; // Fixed size for simplicity
        int _numranks;
        int _current_entry_idx = 0;
        bool _isTracing;
        bool _reportedMaxHit = false;
};
}
#endif // CHROME_TRACER_HH