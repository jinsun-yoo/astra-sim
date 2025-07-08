#ifndef GENIE_NETWORK_HH
#define GENIE_NETWORK_HH

#include <gloo/rendezvous/context.h>

#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/common/Common.hh"


#include "time_keeper.hh"
#include "thread_counter.hh"
#include "qp_manager.hh"

class ASTRASimGenieNetwork : public AstraSim::AstraNetworkAPI {
public:
    ASTRASimGenieNetwork(int rank, std::shared_ptr<gloo::Context> context);
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
private:
    std::shared_ptr<gloo::Context> _context;
    int _send_slot;
    std::mutex* _send_lock;
    int _recv_slot;
    std::shared_ptr<spdlog::logger> _logger;
};

#endif // GENIE_NETWORK_HH
