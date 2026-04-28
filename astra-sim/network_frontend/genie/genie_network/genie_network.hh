#ifndef GENIE_NETWORK_HH
#define GENIE_NETWORK_HH

#include <gloo/rendezvous/context.h>

#include "astra-sim/common/ChromeTracer.hh"
#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/SimpleRing.hh"
#include "astra-sim/common/Common.hh"

#include "time_keeper.hh"
#include "ring_train.hh"
#include "ring_buffer.hh"
#include "thread_counter.hh"
#include "qp_manager.hh"
#include "event_queue.hh"

class ASTRASimGenieNetwork : public AstraSim::AstraNetworkAPI {
public:
    ASTRASimGenieNetwork(int rank, std::shared_ptr<gloo::Context> context, AstraSim::ChromeTracer* chrome_tracer, int nqps);
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
    void poll_send_handler(FuncArgs *fun_arg);
    void sim_send_handler(FuncArgs *fun_arg);
    void poll_recv_handler(FuncArgs *fun_arg);
    void sim_recv_handler(FuncArgs *fun_arg);
    void load_genie_collective(void *incoming_genie_collective_ptr) override {
        this->genie_collective_ptr = static_cast<AstraSim::SimpleRing*>(incoming_genie_collective_ptr);
        // std::cout << "Called load_simple_ring with pending poll send count " << pending_poll_sends.size() << " and pending poll recv count " << pending_poll_recvs.size() << std::endl;
    };
    void unload_genie_collective() override {
        this->genie_collective_ptr = nullptr;
    };
    void mark_complete(int qp_idx, AstraSim::sim_request& snd_req, AstraSim::sim_request& rcv_req, bool is_send);

private:
    std::shared_ptr<gloo::Context> _context;
    std::shared_ptr<spdlog::logger> _logger;
    // Count how many poll events have occured so far. 
    // Used to determine which poll events to record and which to skip (by mod 128, etc.).
    size_t _schedule_poll_counter;
    // Records the receive handler of receive WRs that have not yet been polled.
    RingTrain<SimSendArgs> *sim_send_args;
    RingTrain<SimRecvArgs> *sim_recv_args;
    // one per Rank, for now. We assume this is okay b/c due to hardwareresource, only 1 comm per rank at a time.
    AstraSim::GenieCollective* genie_collective_ptr = nullptr; 
};

#endif // GENIE_NETWORK_HH
