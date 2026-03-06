#include "event.hh"
#include "event_queue.hh"
#include "genie_network.hh"
#include <iostream>
#include <unordered_map>


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
    uint64_t iteration = 0;
    std::unordered_map<EventType, uint64_t> event_counts;
    static constexpr uint64_t LOG_INTERVAL = 10000000; // log every 10M iterations
    while (!events.empty()) {
        Event event = events.front();
        events.pop();
        event_counts[event.get_event_type()]++;
        if (++iteration % LOG_INTERVAL == 0) {
            std::cerr << "[EventQueue::start] iteration=" << iteration
                      << " queue_size=" << events.size() << " event counts:";
            for (auto& [type, count] : event_counts) {
                std::cerr << " " << event_type_to_string(type) << "=" << count;
            }
            std::cerr << std::endl;
        }
        event.trigger_event(network);
    }
    std::cerr << "[EventQueue::start] done after " << iteration << " iterations. Event counts:";
    for (auto& [type, count] : event_counts) {
        std::cerr << " " << event_type_to_string(type) << "=" << count;
    }
    std::cerr << std::endl;
}

bool EventQueue::empty() const {
    return events.empty();
}

size_t EventQueue::size() const {
    return events.size();
}