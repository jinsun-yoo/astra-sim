#ifndef GENT_NETWORK_HH
#define GENT_NETWORK_HH

#include "astra-sim/common/AstraNetworkAPI.hh"
#include "time_keeper.hh"

#include <gloo/rendezvous/context.h>

class ASTRASimGentNetwork : public AstraSim::AstraNetworkAPI {
public:
    ASTRASimGentNetwork(int rank, std::shared_ptr<gloo::rendezvous::Context> context);
    ~ASTRASimGentNetwork();

    void sim_notify_finished() override;
    AstraSim::timespec_t sim_get_time() override;

    virtual void sim_schedule(AstraSim::timespec_t delta,
                              void (*fun_ptr)(void* fun_arg),
                              void* fun_arg) override;

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

private:
    std::shared_ptr<gloo::rendezvous::Context> _context;
};

#endif // GENT_NETWORK_HH
