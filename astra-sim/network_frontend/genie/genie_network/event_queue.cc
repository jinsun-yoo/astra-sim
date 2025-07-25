#include "event_queue.hh"
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

void EventQueue::add_event(const Event &event) {
    events.push(event);
}

void EventQueue::clear_events() {
    while (!events.empty()) {
        events.pop();
    }
}

bool EventQueue::pop_event(Event& event) {
    if (events.empty()) {
        return false;
    }
    event = events.front();
    events.pop();
    return true;
}

void EventQueue::start() {
    while (!events.empty()) {
        Event event = events.front();
        events.pop();
        event.trigger_event(network);
    }
}

bool EventQueue::empty() const {
    return events.empty();
}

size_t EventQueue::size() const {
    return events.size();
}