
#include "thread_counter.hh"

Threadcounter::Threadcounter() {
    _logger = AstraSim::LoggerFactory::get_logger("genie");
    std::lock_guard<std::mutex> lock(_mtx);
    _threadCount = 0;
}

void Threadcounter::IncrementThreadCount() {
    std::lock_guard<std::mutex> lock(_mtx);
    ++_threadCount;
    //logger->info("Increment threadcount to {}", threadCount);
}

void Threadcounter::DecreaseThreadCount() {
    std::lock_guard<std::mutex> lock(_mtx);
    if (_threadCount > 0) {
        --_threadCount;
        //logger->info("Decrement threadcount to {}", threadCount);
        if (_threadCount == 0) {
            _cv.notify_all();
        }
    }
}

void Threadcounter::WaitThreadsJoin() {
    std::unique_lock<std::mutex> lock(_mtx);
    _cv.wait(lock, [this]() { return _threadCount == 0; });
}
