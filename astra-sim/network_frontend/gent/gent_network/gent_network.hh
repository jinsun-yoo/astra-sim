#ifndef GENT_NETWORK_HH
#define GENT_NETWORK_HH

#include "astra-sim/common/AstraNetworkAPI.hh"
#include "time_keeper.hh"
#include "thread_pooler.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/common/Common.hh"

#include <gloo/rendezvous/context.h>

class ASTRASimGentNetwork : public AstraSim::AstraNetworkAPI {
public:
    ASTRASimGentNetwork(int rank, std::shared_ptr<gloo::Context> context);
    ~ASTRASimGentNetwork();

    void sim_all_reduce(uint64_t count) override;
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

    Timekeeper timekeeper;

    Threadpooler* threadpooler;
private:
    std::shared_ptr<gloo::Context> _context;
    int _send_slot;
    int _recv_slot;
};

#endif // GENT_NETWORK_HH
