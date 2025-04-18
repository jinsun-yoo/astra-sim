#ifndef THREAD_POOLER_HH
#define THREAD_POOLER_HH

#include <iostream>
#include <mutex>
#include <condition_variable>

#include "astra-sim/common/Logging.hh"

// Assumption is that as long as at least 1 thread is alive, threadCount is never 0 or less.
class Threadpooler {
private:
    int threadCount;
    std::mutex mtx;
    std::condition_variable cv;
    std::shared_ptr<spdlog::logger> logger;    

public:
    Threadpooler() : threadCount(0){
        logger = AstraSim::LoggerFactory::get_logger("thread_pooler");
    }

    void IncrementThreadCount() {
        std::lock_guard<std::mutex> lock(mtx);
        ++threadCount;
        logger->info("Increment threadcount to {}", threadCount);
    }

    void DecreaseThreadCount() {
        std::lock_guard<std::mutex> lock(mtx);
        if (threadCount > 0) {
            --threadCount;
            logger->info("Decrement threadcount to {}", threadCount);
            if (threadCount == 0) {
                cv.notify_all();
            }
        }
    }

    void StartSession() {
        std::lock_guard<std::mutex> lock(mtx);
        threadCount = 0;
    }

    void WaitThreadsJoin() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return threadCount == 0; });
    }
};

#endif // THREAD_POOLER_HH
