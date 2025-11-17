#include "astra-sim/common/ChromeTracer.hh"
#include <dlfcn.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>

namespace AstraSim {

ChromeEvent::ChromeEvent(){};

std::string ChromeEvent::toJson() const {
    return "{\n"
           "  \"name\":\"" + name + "\",\n"
           "  \"cat\":\"" + category + "\",\n"
           "  \"ph\":\"X\",\n"
           "  \"ts\":" + std::to_string(start_ts_micro) + ",\n"
           "  \"dur\":" + std::to_string(duration_micro) + ",\n"
           "  \"pid\":" + std::to_string(rank) + ",\n"
           "  \"tid\":" + std::to_string(event_type) + "\n"
           "}";
}
void ChromeEvent::postprocess() {
    start_ts_micro = static_cast<float>(start_time_nano / 1000.0L);
    long double duration_nano = end_time_nano - start_time_nano;
    if ((std::fmod(duration_nano, 1000.0L) != 0.0L) && duration_nano > 5.0L) {
        // For events that end/start at the same boostedTick,
        // Floating point calculations may mistakenly overlap them at +- 1 ns level.
        duration_nano -= 5.0L;
    }

    duration_micro = static_cast<float>((duration_nano) / 1000.0L);
    return;
}

void ChromeTracer::get_and_setfilename() {
    char datetime_str[20];
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_now = std::localtime(&now_time);
    std::strftime(datetime_str, sizeof(datetime_str), "%m%d_%H%M%S", tm_now);
    log_filename = std::string("chrome_trace_") + datetime_str + ".json";
    return;
}

ChromeTracer::ChromeTracer(): _current_entry_idx(0), _isTracing(false) {
    get_and_setfilename();
    std::cout << "Log file name: " << log_filename << std::endl;

}

ChromeTracer::~ChromeTracer() {
    std::ofstream ofs = wait_and_get_logfile();

    ofs << "[\n";
    for (size_t i = 0; i < _current_entry_idx; ++i) {
        auto entry = entry_queue[i];
        entry.postprocess();

        ofs << entry.toJson();

        if (i + 1 < _current_entry_idx) ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    close_and_signal_ofs(ofs);
}

void ChromeTracer::close_and_signal_ofs(std::ofstream& ofs) {
    ofs.close();
}

std::ofstream ChromeTracer::wait_and_get_logfile(bool is_poll_recv) {
    std::string filename = log_filename;
    // Generate log filename if not set
    if (filename.empty()) {
        filename = "chrome_trace.json";
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

int ChromeTracer::logEventStart(const std::string& name, const std::string& category, ChromeEventType event_type, long double start_timestamp_nano, int rank) {
    // return -1;
    // Reference approach - clean and efficient
    ChromeEvent& event = entry_queue[_current_entry_idx];
    event.name = name;
    event.category = category;
    event.start_time_nano = start_timestamp_nano;
    event.rank = rank;
    event.event_type = static_cast<int>(event_type);
    _current_entry_idx++;
    if (_current_entry_idx == MAX_QUEUE_SIZE) {
        if (!_reportedMaxHit) {
            std::cout << "Current entry idx hit maximum queue size!" << std::endl;
            _reportedMaxHit = true;
        }
        _current_entry_idx -= 1;
    }
    // std::cout << "Entry at id " << _current_entry_idx - 1<< "name is " << entry_queue[_current_entry_idx-1].name << "start-timestamp is " << event.start_hw_ctr << std::endl;
    return _current_entry_idx -1;
}

void ChromeTracer::logEventEnd(size_t entry_idx, long double end_timestamp_nano) {
    // return;
    ChromeEvent& event = entry_queue[entry_idx];
    event.end_time_nano = end_timestamp_nano;
    return ;
}
}
