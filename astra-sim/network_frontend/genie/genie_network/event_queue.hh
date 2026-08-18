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
#include "ring_buffer.hh"


class ASTRASimGenieNetwork; // Forward declaration
class EventQueue; //Forward declaration

class FuncArgs {
public:
    int stream_id;
    int qp_idx;
};

class SimScheduleArgs : public FuncArgs {
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

class SimSendArgs : public FuncArgs {
public:
    ASTRASimGenieNetwork *network;
    gloo::transport::Buffer *buf;
    uint64_t msg_size;
    void (*msg_handler)(void *fun_arg);
    void *fun_arg;
    EventQueue *event_queue;
    int peer_rank;
    int comm_group_id;
};

class PollSendArgs : public FuncArgs {
public:
    gloo::transport::Buffer *buf;
    void (*msg_handler)(void *fun_arg);
    void *fun_arg;
    int peer_rank;
    int comm_group_id;
};

class PollRecvArgs : public FuncArgs {
public:
    gloo::transport::Buffer *buf;
    void (*msg_handler)(void *fun_arg);
    void *fun_arg;
    int peer_rank;
    int comm_group_id;
};

class SimRecvArgs : public FuncArgs {
public:
    gloo::transport::Buffer *buf;
    void (*msg_handler)(void* fun_arg);
    void* fun_arg;
    EventQueue* event_queue;
    int peer_rank;
    int comm_group_id;
};

class EventQueue {
public:
    EventQueue(ASTRASimGenieNetwork *network) : network(network) {
        events = new RingBuffer<Event>(512, 100);
    };
    ~EventQueue() {
        delete events;
    };

    void add_event(const Event &event);
    bool add_poll_event(const Event &event);
    void clear_events();
    bool pop_event(Event& event);
    void start();
    bool empty() const;
    size_t size() const;
    void print();
    void mark_workload_finished();
    void assert_only_poll_events_remain();
    void reset_for_next_iteration();
    bool is_workload_finished() const;

private:
    RingBuffer<Event> *events;
    ASTRASimGenieNetwork *network; // Pointer to the network for event handling
    bool workload_finished = false;
};

#endif // EVENT_QUEUE_HH