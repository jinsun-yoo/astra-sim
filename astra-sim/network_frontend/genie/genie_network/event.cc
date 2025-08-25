#include "event.hh"
#include "genie_network.hh"

void Event::trigger_event(ASTRASimGenieNetwork *network) {
    switch(event_type){
        case SIM_SEND:
            network->sim_send_handler(func_arg);
            break;
        case POLL_SEND:
            network->poll_send_handler(func_arg);
            break;
        case SIM_RECV:
            network->sim_recv_handler(func_arg);
            break;
        case POLL_RECV:
            network->poll_recv_handler(func_arg);
            break;
        case SCHEDULE_EVENT:
            network->sim_schedule_handler(func_arg);
            break;
        default:
            throw std::runtime_error("Unknown event type");
    }
}