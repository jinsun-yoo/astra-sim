#ifndef CHROME_TRACER_HH
#define CHROME_TRACER_HH

#include <string>
#include <cstdint>
#include <fstream>
constexpr size_t MAX_QUEUE_SIZE =  256 * 1024;

namespace AstraSim {
class ChromeEvent {
    public:
        ChromeEvent(); // Default constructor
        ChromeEvent(const std::string& name, const std::string& category, const std::string& phase, uint64_t timestamp);

        std::string toJson() const;
        void postprocess(size_t first_hw_ctr, float cpu_freq_mhz, int rank);

        std::string name;
        std::string category;
        std::string phase = "X";
        size_t start_hw_ctr;
        size_t start_hw_ctr_diff;
        float start_ts_micro;
        size_t end_hw_ctr;
        float duration_micro;
        int event_type;
        bool did_sleep;
        int pid;
        int tid;
        long double start_time_micro;
        long double end_time_micro;
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
        ChromeTracer(int rank, int numranks);
        ~ChromeTracer();

        void startTrace(const std::string& traceFile);
        void stopTrace();
        int logEventStart(const std::string& name, const std::string& category, int event_type, bool requeue_sleep = true);
        void logEventEnd(size_t entry_idx, bool poll_has_completed = false);
    
    private:
        void get_and_setfilename();
        void set_cpu_freq();
        std::ofstream wait_and_get_logfile(bool is_pollrecv_log = false);
        void close_and_signal_ofs(std::ofstream& ofs);

        std::string log_filename;
        ChromeEvent entry_queue[MAX_QUEUE_SIZE]; // Fixed size for simplicity
        int _rank;
        int _numranks;
        int _current_entry_idx = 0;
        int _current_poll_entry_idx = 0;
        bool _isTracing = false;
        size_t _first_hw_ctr = 0;
        float _cpu_freq_mhz = 0;
};
}
#endif // CHROME_TRACER_HH