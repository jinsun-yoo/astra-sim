#ifndef GENIE_NETWORK_HH
#define GENIE_NETWORK_HH

#include <gloo/rendezvous/context.h>

#include "astra-sim/common/ChromeTracer.hh"
#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/common/Common.hh"

#include "time_keeper.hh"
#include "thread_counter.hh"
#include "qp_manager.hh"
#include "event_queue.hh"

class ASTRASimGenieNetwork : public AstraSim::AstraNetworkAPI {
public:
    ASTRASimGenieNetwork(int rank, std::shared_ptr<gloo::Context> context, AstraSim::ChromeTracer* chrome_tracer);
    ~ASTRASimGenieNetwork();

    void sim_notify_finished() override;
    AstraSim::timespec_t sim_get_time() override;

    virtual void sim_schedule(AstraSim::timespec_t delta,
                              AstraSim::Callable* callable,
                              AstraSim::EventType event,
                              AstraSim::CallData* callData) override;

    virtual int sim_send(void* buffer,
                         uint64_t message_size,
                         int type,
                         int dst_id,
                         int tag,
                         AstraSim::sim_request* request,
                         void (*msg_handler)(void* fun_arg),
                         void* fun_arg) override;

    virtual int sim_recv(void* buffer,
                         uint64_t message_size,
                         int type,
                         int src_id,
                         int tag,
                         AstraSim::sim_request* request,
                         void (*msg_handler)(void* fun_arg),
                         void* fun_arg) override;

    Timekeeper* timekeeper;
    Threadcounter* threadcounter;
    QueuepairManager* qp_manager;
    EventQueue* event_queue;
    AstraSim::ChromeTracer* chrome_tracer;

    // Event handler functions
    void sim_schedule_handler(void *func_arg);
    void poll_send_handler(void *fun_arg);
    void sim_send_handler(void *fun_arg);
    void poll_recv_handler(void *fun_args);
    void sim_recv_handler(void *fun_args);

private:
    std::shared_ptr<gloo::Context> _context;
    int _send_slot;
    std::mutex* _send_lock;
    int _recv_slot;
    std::shared_ptr<spdlog::logger> _logger;
    // Count how many poll events have occured so far. 
    // Used to determine which poll events to record and which to skip (by mod 128, etc.).
    size_t _schedule_poll_counter;
    size_t _poll_recv_counter;
};

#endif // GENIE_NETWORK_HH
