#ifndef EVENT_QUEUE_HH
#define EVENT_QUEUE_HH

#include <string>
#include <queue>
#include <stdexcept>
#include <chrono>
#include "astra-sim/common/Common.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/CallData.hh"
#include <gloo/transport/buffer.h>
#include <gloo/transport/ibverbs/device.h>


class ASTRASimGenieNetwork; // Forward declaration
class EventQueue; //Forward declaration

class SimScheduleArgs {
public:
    AstraSim::timespec_t delta;
    AstraSim::Callable* callable;
    AstraSim::EventType event;
    AstraSim::CallData* call_data;
    std::chrono::steady_clock::time_point start_time;
};
class SimSendArgs {
public:
    ASTRASimGenieNetwork *network;
    gloo::transport::Buffer *buf;
    uint64_t msg_size;
    void (*msg_handler)(void *fun_arg);
    void *fun_arg;
    EventQueue *event_queue;
};
class PollSendArgs {
public:
    gloo::transport::Buffer *buf;
    void (*msg_handler)(void *fun_arg);
    void *fun_arg;
};
class PollRecvArgs {
public:
    gloo::transport::Buffer *buf;
    void (*msg_handler)(void* fun_arg);
    void* fun_arg;
};

class SimRecvArgs {
public:
    gloo::transport::Buffer *buf;
    void (*msg_handler)(void* fun_arg);
    void* fun_arg;
    EventQueue* event_queue;
};

enum EventType {
    SIM_SEND,
    POLL_SEND,
    SIM_RECV,
    POLL_RECV,
    SCHEDULE_EVENT
};

class Event {
public:
    Event(
        EventType event_type,
        void *func_arg, 
        const std::string &description = "") : 
        event_type(event_type), func_arg(func_arg), description(description) {}
    
    void trigger_event(ASTRASimGenieNetwork *network);

    EventType get_event_type() const {
        return event_type;
    }

private:
    EventType event_type;
    void *func_arg;
    std::string description;
};

class EventQueue {
public:
    EventQueue(ASTRASimGenieNetwork *network) : network(network) {};
    ~EventQueue() = default;

    void add_event(const Event &event);
    void add_poll_event(const Event &event);
    void clear_events();
    bool pop_event(Event& event);
    void start();
    bool empty() const;
    size_t size() const;

private:
    std::queue<Event> events;
    ASTRASimGenieNetwork *network; // Pointer to the network for event handling
};

#endif // EVENT_QUEUE_HH