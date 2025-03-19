#include <chrono>

#include "time_keeper.hh"

void Timekeeper::startTime() {
    start = std::chrono::high_resolution_clock::now();
}

long long Timekeeper::elapsedTimeNanoseconds() const {
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}
