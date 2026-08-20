#include "event.hh"
#include "event_queue.hh"
#include "genie_network.hh"
#include <sstream>


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

void EventQueue::assert_only_poll_events_remain() {
    // Fast path: short-circuit scan that only checks event types, with no
    // string building at all. This is the common case (queue holds only
    // POLL_SEND/POLL_RECV), so it must be as cheap as possible.
    bool has_non_poll_event = false;
    events->for_each_until([&](const Event& event) {
        EventType type = event.get_event_type();
        if (type != POLL_SEND && type != POLL_RECV) {
            has_non_poll_event = true;
            return true; // stop scanning, we already know the answer
        }
        return false;
    });

    if (!has_non_poll_event) {
        return;
    }

    // Slow path (rare, error-only): only now do we pay for print_stream()
    // and string concatenation, to build a full dump of the queue for
    // diagnostics. Unlike the old prune_non_poll_events() behavior, no
    // events are ever silently discarded or reordered here.
    std::ostringstream queue_stream;
    bool first = true;
    events->for_each([&](const Event& event) {
        if (!first) {
            queue_stream << ", ";
        }
        first = false;
        queue_stream << event.print_stream();
    });

    network->logger()->critical(
        "EventQueue exited with non-POLL_SEND/POLL_RECV events still queued: {}",
        queue_stream.str());
    throw std::runtime_error(
        "EventQueue exited with non-POLL_SEND/POLL_RECV events remaining "
        "in the queue: " + queue_stream.str());
}

void EventQueue::reset_for_next_iteration() {
    workload_finished = false;
}

bool EventQueue::is_workload_finished() const {
    return workload_finished;
}

void EventQueue::start() {

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
        network->logger()->info("Start event loop with timeout of {} seconds", GENIE_TIMEOUT_SECONDS);
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
                network->logger()->warn("Rank {} exit after timeout", network->rank);
                assert_only_poll_events_remain();
                network->logger()->warn("Timed out while containing only poll events");
                return;
            }
            counter = 0;
        }
        #endif
    }
    if (workload_finished) {
        assert_only_poll_events_remain();
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
        network->logger()->debug("(empty)");
        return;
    }

    // Temporarily store events to preserve the queue
    std::vector<Event> temp_events;
    temp_events.reserve(num_events);
    
    // Dequeue all events
    while (!events->is_empty()) {
        temp_events.push_back(events->dequeue());
    }
    
    std::ostringstream queue_stream;
    queue_stream << temp_events[0].print_stream();

    for (int i = 1; i < num_events; i++) {
        queue_stream << ", " << temp_events[i].print_stream();
    }
    network->logger()->debug("{}", queue_stream.str());
    
    // Re-enqueue all events in the same order
    for (int i = 0; i < num_events; i++) {
        events->enqueue(temp_events[i]);
    }
}

void EventQueue::mark_workload_finished() {
    workload_finished = true;
}
