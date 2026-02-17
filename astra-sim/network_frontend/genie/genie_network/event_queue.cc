#include "event.hh"
#include "event_queue.hh"
#include "genie_network.hh"


void EventQueue::add_event(const Event &event) {
    events.push(event);
}

bool EventQueue::add_poll_event(const Event &event) {
    bool did_sleep = false;
    // This event itself is still enqueued.
    if (events.size() == 1) {
        did_sleep = true;
    }
    events.push(event);
    return did_sleep;
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
    for (int qp_idx = 0; qp_idx < 2; qp_idx ++) {
        PollRecvArgs *recv_args = new PollRecvArgs{
            -1,
            qp_idx,
            network->qp_manager->recv_buffers[qp_idx],
            nullptr,
            nullptr,
        };
        Event recv_event(POLL_RECV, recv_args);
        events.push(recv_event);

        PollSendArgs *send_args = new PollSendArgs{
            -1,
            qp_idx,
            network->qp_manager->send_buffers[qp_idx],
            nullptr,
            nullptr,
        };
        Event send_event(POLL_SEND, send_args);
        events.push(send_event);
    } 

    // When profiling with perf, we want to know the specific time range where the collective starts/ends.
    // We mark this with a harmless syscall, and use 'perf record -e syscalls:sys_enter_getcwd' to capture this time range in perf.
    // This is later visible with 'perf script | grep getcwd'.
    char buf[1024];
    getcwd(buf, sizeof(buf));
    
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

void EventQueue::print() {
    std::queue<Event> temp_queue = events; // Create a copy to iterate through
    int num_events = temp_queue.size();

    Event event = temp_queue.front();
    std::cout << event.print_stream();
    temp_queue.pop();
    int printed_events = 1;
    while (printed_events < num_events) {
        event = temp_queue.front();
        std::cout << ", " << event.print_stream();
        temp_queue.pop();
        printed_events++;
    }
    std::cout << std::endl;
}
