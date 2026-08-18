#include "astra-sim/common/ChromeTracer.hh"
#include <dlfcn.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <x86intrin.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <filesystem>

// Refer to the comment in the constructor.
#ifdef GLOO_USE_MPI
#include <mpi.h>
#endif

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
    if (completed_poll) {
        name = name + "_COMPLETE";
    }
    // if (did_sleep) {
    //     tid = 6;
    // }
    pid = rank;
    return;
}

// Each rank now writes its own trace file independently, so there is no need
// for any MPI coordination (broadcasting a shared datetime string, etc.) to
// pick a filename.
void ChromeTracer::get_and_setfilename() {
    std::string filename = "chrome_trace";

    const char* job_tag = std::getenv("JOBTAG");
    if (job_tag != nullptr && job_tag[0] != '\0') {
        filename += "_" + std::string(job_tag);
    }
    filename += "_" + std::to_string(_rank) + ".json";

    // Get the directory to write in.
    std::string filedir = "./";
    const char* output_path = std::getenv("OUTPUT_PATH");
    if (output_path != nullptr && output_path[0] != '\0') {
        std::filesystem::path candidate(output_path);
        if (std::filesystem::exists(candidate) &&
            std::filesystem::is_directory(candidate)) {
            filedir = candidate.string() + "/chrometrace";
            std::filesystem::create_directories(filedir);
        }
    }

    chrometrace_filepath = std::string(filedir) + "/" + filename;
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
    // _cpu_freq_mhz = 2445;
    _cpu_freq_mhz = 2800;
    std::cout << "CPU Freq is " << _cpu_freq_mhz;
    return;
}

ChromeTracer::ChromeTracer(int rank, int numranks) 
    : _rank(rank), _numranks(numranks), _current_entry_idx(0), _isTracing(false) {
    logger = AstraSim::LoggerFactory::get_logger("chrometracer");
// Each rank writes its trace events to its own JSON file (see get_and_setfilename()),
// so no cross-rank file coordination is needed. MPI is still used to synchronize the
// starting timestamp (_first_hw_ctr) across ranks.
    get_and_setfilename();
    logger->info("Chrometrace file path: {}", chrometrace_filepath);

    // Get the frequency of the specific CPU core this program is running on at runtime (in MHz)
    set_cpu_freq();

#ifdef GLOO_USE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#else 
    logger->warn("Warning: ChromeTracer can only be used with MPI support. Disabling ChromeTracer.");
#endif
    _first_hw_ctr = rdtscp_intrinsic();

}

ChromeTracer::~ChromeTracer() {
#ifdef GLOO_USE_MPI
    std::ofstream ofs(chrometrace_filepath, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        logger->warn("Error: Unable to open log file {}.", chrometrace_filepath);
        return;
    }

    ofs << "[\n";
    for (size_t i = 0; i < _current_entry_idx; ++i) {
        auto entry = entry_queue[i];
        if (i == 0) {
            _first_hw_ctr = entry.start_hw_ctr;
        }
        entry.postprocess(_first_hw_ctr, _cpu_freq_mhz, _rank);
        ofs << entry.toJson();

        if (i + 1 < _current_entry_idx) ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";

    ofs.close();
#endif
}

int ChromeTracer::logEventStart(const std::string& name, const std::string& category, int event_type, bool did_sleep) {
#if GLOO_USE_MPI
    if (_current_entry_idx == CHROMETRACE_QUEUE_SIZE) {
        if (! _notified_current_entry_max) {
            logger->warn("Current entry idx hit maximum queue size! Disable logging further events.");
            _notified_current_entry_max = true;
        }
        return -100;
    }
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
    // std::cout << "Entry at id " << _current_entry_idx - 3<< "name is " << entry_queue[_current_entry_idx-1].name << "start-timestamp is " << event.start_hw_ctr << std::endl;
    return _current_entry_idx -1;
#else 
    return 0;
#endif
}

void ChromeTracer::logEventEnd(int entry_idx, bool poll_has_completed) {
#if GLOO_USE_MPI
    if (entry_idx < 0 || entry_idx == CHROMETRACE_QUEUE_SIZE) {
        return;
    }
    ChromeEvent& event = entry_queue[entry_idx];
    // event.end_time_micro = std::chrono::duration_cast<std::chrono::microseconds>(
    //     std::chrono::steady_clock::now().time_since_epoch()
    // ).count();
    // ).count();
    event.end_hw_ctr = rdtscp_intrinsic();
    if (poll_has_completed) {
        event.completed_poll = true;
    }
#endif

    // std::cout << "Event at " << entry_idx << " end at " << event.end_hw_ctr << std::endl;
}

void ChromeTracer::ignore_last_call() {
    if (_current_entry_idx > 0) {
        _current_entry_idx -= 1;
    }
    return;
}

}
