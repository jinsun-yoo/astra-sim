#include "astra-sim/common/ChromeTracer.hh"
#include <mpi.h>
#include <dlfcn.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <x86intrin.h>

static inline uint64_t rdtscp_intrinsic(void) {
    unsigned int aux;
    return __rdtscp(&aux);
}

namespace AstraSim {

ChromeEvent::ChromeEvent(){};

std::string ChromeEvent::toJson() const {
    return "{\n"
           "  \"name\":\"" + name + "\",\n"
           "  \"cat\":\"" + category + "\",\n"
           "  \"ph\":\"X\",\n"
           "  \"ts\":" + std::to_string(start_ts_micro) + ",\n"
           "  \"dur\":" + std::to_string(duration_micro) + ",\n"
           "  \"pid\":" + std::to_string(pid) + ",\n"
           "  \"tid\":" + std::to_string(tid) + "\n"
        //    "  \"tid\":" + std::to_string(tid) + ",\n"
        //    "  \"args\": {\n"
        //    "      \"start_time_micro\": " + std::to_string(start_time_micro) + ",\n"
        //    "      \"end_time_micro\": " + std::to_string(end_time_micro) + ",\n"
        //    "      \"start_time_clk\": " + std::to_string(start_hw_ctr) + ",\n"
        //    "      \"end_time_clk\": " + std::to_string(end_hw_ctr) + ",\n"
        //    "      \"first_hw_ctr\": " + std::to_string(start_hw_ctr_diff) + ",\n"
        //    "  }\n"
           "}";
}

void ChromeEvent::postprocess(size_t first_hw_ctr, float cpu_freq, int rank) {
    start_hw_ctr_diff = start_hw_ctr - first_hw_ctr;
    start_ts_micro = start_hw_ctr_diff / cpu_freq;
    duration_micro = (end_hw_ctr - start_hw_ctr) / cpu_freq;
    tid = event_type;
    // if (did_sleep) {
    //     tid = 6;
    // }
    pid = rank;
    return;
}

void ChromeTracer::get_and_setfilename() {
    char datetime_str[20];
    if (_rank == 0) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_now = std::localtime(&now_time);
        std::strftime(datetime_str, sizeof(datetime_str), "%m%d_%H%M%S", tm_now);
    }
    MPI_Bcast(datetime_str, sizeof(datetime_str), MPI_CHAR, 0, MPI_COMM_WORLD);
    log_filename = std::string("chrome_trace_") + datetime_str + ".json";
    log_poll_filename = std::string("chrome_trace_pollrecv_") + datetime_str + ".csv";
    return;
}

void ChromeTracer::set_cpu_freq() {
    // Get the frequency of the specific CPU core this program is running on at runtime (in MHz)
    // int cpu = sched_getcpu();
    // float cpu_freq_mhz;
    // std::string freq_path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq";
    // std::ifstream freq_file(freq_path);
    // if (freq_file.is_open()) {
    //     float freq_khz;
    //     freq_file >> freq_khz;
    //     cpu_freq_mhz = freq_khz / 1000.0;
    //     freq_file.close();
    //     std::cout << "Rank " << _rank << " running on CPU " << cpu << " with frequency " << cpu_freq_mhz << " MHz" << std::endl;
    // } else {
    //     cpu_freq_mhz = 0.0;
    //     std::cerr << "Warning: Unable to read CPU frequency from " << freq_path << std::endl;
    // }
    // Placeholder implementation for CPU frequency setting
    // Add actual implementation if needed
    // _cpu_freq_mhz = cpu_freq_mhz;
    _cpu_freq_mhz = 2445;
    std::cout << "CPU Freq is " << _cpu_freq_mhz;
    return;
}

ChromeTracer::ChromeTracer(int rank, int numranks) 
    : _rank(rank), _numranks(numranks), _current_entry_idx(0), _isTracing(false) {

    get_and_setfilename();
    std::cout << "Log file name: " << log_filename << std::endl;

    // Get the frequency of the specific CPU core this program is running on at runtime (in MHz)
    set_cpu_freq();

    MPI_Barrier(MPI_COMM_WORLD);
    _first_hw_ctr = rdtscp_intrinsic();

}

ChromeTracer::~ChromeTracer() {
    std::ofstream ofs = wait_and_get_logfile();

    if (_rank == 0) {
        ofs << "[\n";
    }
    // if (_rank == 0) {
    for (size_t i = 0; i < _current_entry_idx; ++i) {
        auto entry = entry_queue[i];
        if (i == 0) {
            _first_hw_ctr = entry.start_hw_ctr;
        }
        entry.postprocess(_first_hw_ctr, _cpu_freq_mhz, _rank);

        ofs << entry.toJson();

        if (_rank != _numranks - 1 || i + 1 < _current_entry_idx) ofs << ",";
        ofs << "\n";
    }
    // }
    if (_rank == _numranks - 1) {
        ofs << "]\n";
    }

    close_and_signal_ofs(ofs);
    std::ofstream perfrecvfs = wait_and_get_logfile(true);
    perfrecvfs << _rank << ", " << _rank << "," << _rank << "," << _rank << "," << _rank << "," << _rank << "," << _rank << std::endl;
    for (size_t i = 0; i < _current_poll_entry_idx; ++i) {
        auto entry = logpoll_queue[i];
        entry.logstartdur = entry.logstartdur * 1000.0 / 2450;
        entry.polldur = entry.polldur * 1000.0/2450;
        entry.logcompleteenddur = entry.logcompleteenddur * 1000.0/2450;
        entry.msghandlerdur = entry.msghandlerdur * 1000.0 / 2450;
        entry.addpolldur = entry.addpolldur * 1000.0 / 2450;
        entry.logenddur = entry.logenddur * 1000.0 / 2450;
        perfrecvfs << entry.logstartdur << "," << entry.polldur << "," << entry.logcompleteenddur << "," << entry.msghandlerdur << "," << entry.eventconstrdur << "," << entry.addpolldur << "," << entry.logenddur << "\n";
    }
    close_and_signal_ofs(perfrecvfs);
}

void ChromeTracer::close_and_signal_ofs(std::ofstream& ofs) {
    ofs.close();

    if (_rank != _numranks - 1) {
        int next_rank = _rank + 1;
        int message = 1; // Example message
        std::cout << "Rank " << _rank << " Start send message to" << next_rank << std::endl;
        MPI_Send(&message, 1, MPI_INT, next_rank, 0, MPI_COMM_WORLD);
        std::cout << "Rank " << _rank << " Complete send message to" << next_rank << std::endl;
    }
}   

std::ofstream ChromeTracer::wait_and_get_logfile(bool is_poll_recv) {
    std::string filename = log_filename;
    if (is_poll_recv) {
        filename = log_poll_filename;
    }
    if (_rank != 0) {
        int prev_rank = _rank - 1;
        int message;
        std::cout << "Rank " << _rank << " Start recv message from" << prev_rank << std::endl;
        MPI_Recv(&message, 1, MPI_INT, prev_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        std::cout << "Rank " << _rank << " Complete recv message from" << prev_rank << std::endl;
        // Optionally, you can use recv_str for logging or debugging
    }
    // Generate log filename if not set
    if (filename.empty()) {
        std::ostringstream oss;
        oss << "chrome_trace_rank_" << _rank << ".json";
        filename = oss.str();
    }

    std::ofstream ofs(filename, std::ios::app);
    if (!ofs.is_open()) {
        std::cerr << "Error: Unable to open log file " << filename << std::endl;
        return std::ofstream();
    }

    return ofs;
}

void ChromeTracer::startTrace(const std::string& traceFile) {
    this->log_filename = traceFile;
    _isTracing = true;
}

void ChromeTracer::stopTrace() {
    _isTracing = false;
}

int ChromeTracer::logEventStart(const std::string& name, const std::string& category, int event_type, bool did_sleep) {
    // Reference approach - clean and efficient
    ChromeEvent& event = entry_queue[_current_entry_idx];
    event.name = name;
    event.category = category;
    event.event_type = event_type;
    event.did_sleep = did_sleep;
    event.event_type = event_type;
    // event.start_time_micro = std::chrono::duration_cast<std::chrono::microseconds>(
    //     std::chrono::steady_clock::now().time_since_epoch()
    // ).count();
    event.start_hw_ctr = rdtscp_intrinsic();
    
    _current_entry_idx++;
    if (_current_entry_idx == MAX_QUEUE_SIZE) {
        std::cout << "Current entry idx hit maximum queue size!" << std::endl;
        _current_entry_idx -= 1;
    }
    // std::cout << "Entry at id " << _current_entry_idx - 1<< "name is " << entry_queue[_current_entry_idx-1].name << "start-timestamp is " << event.start_hw_ctr << std::endl;
    return _current_entry_idx -1;
}

void ChromeTracer::logEventEnd(size_t entry_idx, bool poll_has_completed) {
    ChromeEvent& event = entry_queue[entry_idx];
    // event.end_time_micro = std::chrono::duration_cast<std::chrono::microseconds>(
    //     std::chrono::steady_clock::now().time_since_epoch()
    // ).count();
    event.end_hw_ctr = rdtscp_intrinsic();
    if (poll_has_completed) {
        event.name = event.name + "_COMPLETE";
    }

    // std::cout << "Event at " << entry_idx << " end at " << event.end_hw_ctr << std::endl;
}
}
