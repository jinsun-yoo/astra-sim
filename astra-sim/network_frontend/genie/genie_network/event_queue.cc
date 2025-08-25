#include "event.hh"
#include "event_queue.hh"
#include "genie_network.hh"


void EventQueue::add_event(const Event &event) {
    events.push(event);
}

void EventQueue::add_poll_event(const Event &event) {
    if (events.size() == 0) {
        // Sleep for 1ms to give room to queue when noone is waiting.
        usleep(10);
    }
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