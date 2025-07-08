#ifndef THREAD_POOLER_HH
#define THREAD_POOLER_HH

#include <iostream>
#include <mutex>
#include <condition_variable>

#include "astra-sim/common/Logging.hh"

// Each call to sim_send or sim_recv spawns a new thread. This is to mimic the behavior of doing compute (or other jobs) locally while the message is being sent.
// However, we do not use pthread_join, because that would block the simulation. 
// This counter's role is to keep track of how many threads are active, and wait at the end of the emulation until all threads have completed.
// TODO: We do not want to create a thread every time sim_send/recv is called. Extend this to have pooling feature.
// TODO: Will wait forever if a thread fails before calling DecreaseThreadCount.    
// TODO: Does it make sense to create threads where we are creating them right now (Within ASTRASimGenieNetwork)? 
// TODO: The assumption, which I haven't formally verified, is that the number of threads will always be greater than 0 as long as the workload is running, 
// and that it will go to 0 only at the end of the workload. 
class Threadcounter {
public:
    Threadcounter();
    // Call before spawning a new thread. Will increment 'threadCount' by 1.
    void IncrementThreadCount();
    // Call before ending a thread. Will decrement 'threadCount' by 1. 
    void DecreaseThreadCount();
    // Will wait until 'threadCount' is 0 (i.e. all threads have completed)
    void WaitThreadsJoin();

private:
    // Number of active threads.
    int _threadCount;
    std::mutex _mtx;
    // Will signal when active threads is 0. 
    std::condition_variable _cv;
    std::shared_ptr<spdlog::logger> _logger;    
};

#endif // THREAD_POOLER_HH
