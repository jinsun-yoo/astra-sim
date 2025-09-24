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
        // Sleep for 10us to give room to queue when noone is waiting.
        // Seems like sleeping results in a much longer delay/descheduling.
        // usleep(10);
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