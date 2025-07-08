#include <chrono>
#include <stdexcept>

#include "time_keeper.hh"

void Timekeeper::startTimer() {
    if (_is_started) {
        throw std::runtime_error("Timer has already been started.");
    }
    _is_started = true;
    _startTime = std::chrono::high_resolution_clock::now();
}

long long Timekeeper::elapsedTimeNanoseconds() const {
    auto _currentTime = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(_currentTime - _startTime).count();
}
