#include "event.hh"
#include "event_queue.hh"
#include "genie_network.hh"


void EventQueue::add_event(const Event &event) {
    events->enqueue(event);
}

bool EventQueue::add_poll_event(const Event &event) {
    bool did_sleep = false;
    // This event itself is still enqueued.
    if (events->size() == 1) {
        did_sleep = true;
    }
    events->enqueue(event);
    return did_sleep;
}

void EventQueue::clear_events() {
    while (!events->is_empty()) {
        events->dequeue();
    }
}

bool EventQueue::pop_event(Event& event) {
    if (events->is_empty()) {
        return false;
    }
    event = events->dequeue();
    return true;
}

void EventQueue::start() {
    for (int qp_idx = 0; qp_idx < NUM_QPS; qp_idx ++) {
        PollRecvArgs *recv_args = new PollRecvArgs{
            -1,
            qp_idx,
            network->qp_manager->recv_buffers[qp_idx],
            nullptr,
            nullptr,
        };
        Event recv_event(POLL_RECV, recv_args);
        events->enqueue(recv_event);

        PollSendArgs *send_args = new PollSendArgs{
            -1,
            qp_idx,
            network->qp_manager->send_buffers[qp_idx],
            nullptr,
            nullptr,
        };
        Event send_event(POLL_SEND, send_args);
        events->enqueue(send_event);
    } 

    // When profiling with perf, we want to know the specific time range where the collective starts/ends.
    // We mark this with a harmless syscall, and use 'perf record -e syscalls:sys_enter_getcwd' to capture this time range in perf.
    // This is later visible with 'perf script | grep getcwd'.
    char buf[1024];
    getcwd(buf, sizeof(buf));
    
    // When there is only 'POLL_XXX' in the event queue, we do not know if 1) Everything has completed or 2) we are waiting for some events->
    // Therefore, use sim_notify_finished to trigger the 'workload_finished' variable, to exit the while loop
    // These macros are defined at the top CMakeLists.txt
    #ifdef GENIE_TIMEOUT
    if (network->rank == 0){
        std::cout << "Start event loop with timeout of " << GENIE_TIMEOUT_SECONDS << " seconds" << std::endl;
    }
    int counter = 0;
    auto start_time = std::chrono::steady_clock::now();
    #endif
    while (!empty() && !workload_finished) {
        Event event = events->dequeue();
        event.trigger_event(network);
        #ifdef GENIE_TIMEOUT
        counter += 1;
        if (counter % 100000 == 0) {
            auto end_time = std::chrono::steady_clock::now();
            if (end_time - start_time > std::chrono::seconds(GENIE_TIMEOUT_SECONDS)) {
                std::cout << "Rank " << network->rank << " exit after timeout"; 
                return;
            }
            counter = 0;
        }
        #endif
    }
    getcwd(buf, sizeof(buf));
}

bool EventQueue::empty() const {
    return events->is_empty();
}

size_t EventQueue::size() const {
    return events->size();
}

void EventQueue::print() {
    int num_events = events->size();
    if (num_events == 0) {
        std::cout << "(empty)" << std::endl;
        return;
    }

    // Temporarily store events to preserve the queue
    std::vector<Event> temp_events;
    temp_events.reserve(num_events);
    
    // Dequeue all events
    while (!events->is_empty()) {
        temp_events.push_back(events->dequeue());
    }
    
    // Print the first event
    std::cout << temp_events[0].print_stream();
    
    // Print remaining events
    for (int i = 1; i < num_events; i++) {
        std::cout << ", " << temp_events[i].print_stream();
    }
    std::cout << std::endl;
    
    // Re-enqueue all events in the same order
    for (int i = 0; i < num_events; i++) {
        events->enqueue(temp_events[i]);
    }
}

void EventQueue::mark_workload_finished() {
    workload_finished = true;
}
