#include "event.hh"
#include "event_queue.hh"
#include "genie_network.hh"

std::string Event::print_stream() const {
    return "[" + event_type_to_string(event_type) + 
           ":" + std::to_string(func_arg->stream_id) + "]";
}

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