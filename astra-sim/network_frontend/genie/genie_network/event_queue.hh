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
#include "event.hh"


class ASTRASimGenieNetwork; // Forward declaration
class EventQueue; //Forward declaration

class SimScheduleArgs {
public:
    AstraSim::timespec_t delta;
    AstraSim::Callable* callable;
    AstraSim::EventType event;
    AstraSim::CallData* call_data;
    std::chrono::steady_clock::time_point start_time;
    std::string event_name;
    int event_id;
    bool is_gpu;
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

class EventQueue {
public:
    EventQueue(ASTRASimGenieNetwork *network) : network(network) {};
    ~EventQueue() = default;

    void add_event(const Event &event);
    bool add_poll_event(const Event &event);
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