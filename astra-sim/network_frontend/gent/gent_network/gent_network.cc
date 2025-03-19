#include "gent_network.hh"

ASTRASimGentNetwork::ASTRASimGentNetwork(int rank, std::shared_ptr<gloo::rendezvous::Context> context)
    : AstraSim::AstraNetworkAPI(rank), _context(context), timekeeper() {}

ASTRASimGentNetwork::~ASTRASimGentNetwork() {}

void ASTRASimGentNetwork::sim_notify_finished() {
    return;
}

AstraSim::timespec_t ASTRASimGentNetwork::sim_get_time() {
    AstraSim::timespec_t timeSpec;
    timeSpec.time_res = AstraSim::NS;
    timeSpec.time_val = 1000;
    return timeSpec;
}

void ASTRASimGentNetwork::sim_schedule(AstraSim::timespec_t delta,
                                       void (*fun_ptr)(void* fun_arg),
                                       void* fun_arg) {
    return;
}

int ASTRASimGentNetwork::sim_send(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int dst_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    return 0;
}

int ASTRASimGentNetwork::sim_recv(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int src_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    return 0;
}
