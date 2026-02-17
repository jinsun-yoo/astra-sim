#include <thread>

#include "genie_network.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/WorkloadLayerHandlerData.hh"
#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/common/Common.hh"
#include <chrono>
#include <x86intrin.h>

#ifdef GENIE_CHROMETRACE_EVENT
static inline uint64_t rdtscp_intrinsic(void) {
    unsigned int aux;
    return __rdtscp(&aux);
}
#endif

ASTRASimGenieNetwork::ASTRASimGenieNetwork(int rank, std::shared_ptr<gloo::Context> context, AstraSim::ChromeTracer* chrome_tracer, int nqps)
    : AstraSim::AstraNetworkAPI(rank), _context(context), _send_slot(0), _recv_slot(0), chrome_tracer(chrome_tracer), _schedule_poll_counter(0), _poll_recv_counter(0) {
        threadcounter = new Threadcounter();
        timekeeper = new Timekeeper();
        _logger = AstraSim::LoggerFactory::get_logger("genie");
        // TODO: This assumes a ring collective of contiguous NPUs.
        int right_rank = (rank + 1 + context->size) % context->size;
        int left_rank = (rank - 1 + context->size) % context->size;
        qp_manager = new QueuepairManager(context->transportContext_, _logger, right_rank, left_rank, nqps);
        _send_lock = new std::mutex();
        event_queue = new EventQueue(this);
        ring_buffer_recv_args[0] = new RingBuffer(16, 0);
        ring_buffer_recv_args[1] = new RingBuffer(16, 1);
    }

ASTRASimGenieNetwork::~ASTRASimGenieNetwork() {
}

void ASTRASimGenieNetwork::sim_notify_finished() {
    return;
}

AstraSim::timespec_t ASTRASimGenieNetwork::sim_get_time() {
    AstraSim::timespec_t timeSpec;
    timeSpec.time_res = AstraSim::NS;
    timeSpec.time_val = timekeeper->elapsedTimeNanoseconds();
    return timeSpec;
}

void ASTRASimGenieNetwork::sim_schedule(AstraSim::timespec_t delta,
                                       AstraSim::Callable* callable,
                                       AstraSim::EventType event_type,
                                       AstraSim::CallData* callData) {
    auto start_time = std::chrono::steady_clock::now();
    bool can_cast = true;
    AstraSim::WorkloadLayerHandlerData* wlhd = nullptr;
    wlhd = dynamic_cast<AstraSim::WorkloadLayerHandlerData*>(callData);
    std::string event_name = "";
    int event_id = 0;
    bool is_gpu = false;
    if (wlhd) {
        can_cast = false;
        event_name = wlhd->name;
        event_id = wlhd->node_id;
        is_gpu = wlhd->is_gpu;
    }
    SimScheduleArgs *event_args = new SimScheduleArgs {
        -1,
        -1,
        delta,
        callable,
        event_type,
        callData,
        start_time,
        event_name,
        event_id,
        is_gpu
    };
    Event event(SCHEDULE_EVENT, event_args);
    event_queue->add_event(event);

    // sim_schedule is largely called for 1) Compute operations, 2) Reduce computations, and 3) Modelling data movement between NPU and MA. 
    // For now, we return immediately (to avoid making debugging etc complicated). i.e. we do not model the above compute operations.
    // TODO: Revive the code below. 
    #ifdef FALSE
    //auto start_time = timekeeper->elapsedTimeNanoseconds();
    threadcounter->IncrementThreadCount();
    //auto increment_time = timekeeper->elapsedTimeNanoseconds();
    pthread_t thread;
    struct ThreadArgs {
        AstraSim::timespec_t delta;
        AstraSim::Callable* callable;
        AstraSim::EventType event;
        AstraSim::CallData* callData;
        Threadcounter* threadcounter;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);
        //auto sleep_start_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        if (threadArgs->delta.time_res == AstraSim::NS) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        } else if (threadArgs->delta.time_res == AstraSim::US) {
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        } else if (threadArgs->delta.time_res == AstraSim::MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        }
        //auto sleep_end_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        std::cout << "Sim_Schedule with sleep_time " << threadArgs->delta.time_val << " and resolution " << threadArgs->delta.time_res  << std::endl;
        //<< " start_time " << threadArgs->start_time << " increment_time " << threadArgs->increment_time << " sleep_start_time " << sleep_start_time  << " sleep_end_time " << sleep_end_time << std::endl;
        threadArgs->callable->call(threadArgs->event, threadArgs->callData);
        threadArgs->threadcounter->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{delta, callable, event, callData, threadcounter};
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return;
    #endif
}

int ASTRASimGenieNetwork::sim_send(void* buffer,
                                  uint64_t msg_size,
                                  int type,
                                  int dst_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    // TODO: The buffer index and the QP is hardcoded here. 
    // int send_buf_idx = threadArgs->send_buf_idx;
    int qp_idx = tag;
    auto buf = qp_manager->send_buffers[qp_idx];

    SimSendArgs *event_args = new SimSendArgs{
        request->tag, // stream_id
        qp_idx, // qp_idx
        this,           // network
        buf,   // send_buf_idx  
        msg_size, //msg_size
        msg_handler,    // msg_handler
        fun_arg,        // fun_arg
        event_queue     // event_queue
    };
    Event event(SIM_SEND, event_args);
    event_queue->add_event(event);
    

    // TODO: The message size is fixed to the buffer size. 
    // Must send only 'message_size' bytes.
    
    // buf->waitSend();
    // long long send_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
    //                     std::chrono::system_clock::now().time_since_epoch())
    //                     .count();
    //threadArgs->logger->debug("Send Complete");
    return 0;
}

int ASTRASimGenieNetwork::sim_recv(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int src_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    long long recv_start_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    // TODO: The buffer index and the QP is hardcoded here. 
    int qp_idx = tag;
    auto buf = qp_manager->recv_buffers[qp_idx];
    auto event_args = new SimRecvArgs {
        request->tag, // stream_id
        qp_idx, // qp_idx
        buf,
        msg_handler, 
        fun_arg,
        event_queue,
    };
    Event event(SIM_RECV, event_args);
    event_queue->add_event(event);
    // TODO: Does it make sense not to create a thread here, when waitSend is in a detached thread?
    // buf->waitRecv();
    long long recv_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    //logger->debug("Recv from {} to of size {} at time {}", src_id, message_size, recv_start_time);
    //logger->debug("Recv complete from {} of size {} at time {}", src_id, message_size, recv_end_time);

    // msg_handler(fun_arg);
    return 0;
}

void ASTRASimGenieNetwork::sim_schedule_handler(void *func_arg) {
    SimScheduleArgs *args = static_cast<SimScheduleArgs*>(func_arg);
    if (!args) {
        throw std::runtime_error("null argument to sim_schedule_handler");
    }
    if (args->delta.time_res != AstraSim::time_type_e::NS) {
        throw std::runtime_error("Very unlikely: Time resolution for sim_schedule is not NS: " + std::to_string(args->delta.time_res));
    }

    #ifdef GENIE_CHROMETRACE_EVENT
    char event_name_buf[16];
    int len = snprintf(event_name_buf, sizeof(event_name_buf), "%d", args->event_id);
    static constexpr const char* SIM_SCHED_EVENT_STR = "SCHED_EVENT";
    EventType event_type = SCHEDULE_EVENT;
    int chrometrace_entry_idx = chrome_tracer->logEventStart(event_name_buf, SIM_SCHED_EVENT_STR, event_type);
    #endif

    auto current_time = std::chrono::steady_clock::now();
    auto duration = current_time - args->start_time;
    long double duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    if (args->delta.time_val > duration_ns) {
        // Still need to wait more
        Event event(SCHEDULE_EVENT, args);
        bool did_sleep = event_queue->add_poll_event(event);

        #ifdef GENIE_CHROMETRACE_EVENT
        // Only log every POLL_SKIP_MOD requests.
        if (!((_schedule_poll_counter % POLL_SKIP_MOD_INTERVAL))) {
            chrome_tracer->logEventEnd(chrometrace_entry_idx);
        } else {
            chrome_tracer->ignore_last_call();
        }
        _schedule_poll_counter += 1;
        #endif

    } else {
        // Event has completed. Trigger workload to move onto next step.
        args->callable->call(args->event, args->call_data);

        #ifdef GENIE_CHROMETRACE_EVENT
        chrome_tracer->logEventEnd(chrometrace_entry_idx, true);
        #endif
        delete args;
    }
    return;
}

void ASTRASimGenieNetwork::poll_send_handler(FuncArgs *fun_arg) {
    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* POLL_SEND_EVENT_NAME = "POLL_SEND";
    static constexpr const char* POLL_SEND_EVENT_STR = "POLL_SEND_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(POLL_SEND_EVENT_NAME, POLL_SEND_EVENT_STR, POLL_SEND);
    #endif
    PollSendArgs *args = static_cast<PollSendArgs*>(fun_arg);
    if (!args) {
        throw std::runtime_error("null argument to poll_send_handler");
    }

    int qp_idx = args->qp_idx; // Replacing 'stream_id' with QP idx
    auto sendComplete = qp_manager->send_buffers[qp_idx]->pollQP();

    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx, sendComplete > 0);
    #endif

    Event event(POLL_SEND, fun_arg);
    event_queue->add_poll_event(event);
    return;
}

void ASTRASimGenieNetwork::sim_send_handler(FuncArgs *fun_arg) {
    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* SIM_SEND_EVENT_NAME = "SIM_SEND";
    static constexpr const char* SIM_SEND_EVENT_STR = "SIM_SEND_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(SIM_SEND_EVENT_NAME, SIM_SEND_EVENT_STR, SIM_SEND);
    #endif
    SimSendArgs *args = static_cast<SimSendArgs*>(fun_arg);
    if (!args) {
        throw std::runtime_error("null argument to sim_send_handler");
    }

    args->buf->send(0, args->msg_size, 0, args->stream_id);

    delete args;
    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx);
    #endif
    return;
}

void ASTRASimGenieNetwork::poll_recv_handler(FuncArgs *fun_args) {

    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* POLL_RECV_EVENT_NAME = "POLL_RECV";
    static constexpr const char* POLL_RECV_EVENT_STR = "POLL_RECV_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(POLL_RECV_EVENT_NAME, POLL_RECV_EVENT_STR, POLL_RECV);
    #endif
    auto args = static_cast<PollRecvArgs*>(fun_args);


    auto recvComplete = args->buf->pollQP();
    int qp_idx = args->qp_idx; // Get qp_idx directly from args. SimpleRing makes it impossible to infer qp_idx from stream_id.

    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx, cqe_idx > 0);
    #endif

    for (int cqe_idx = 0; cqe_idx < recvComplete; cqe_idx++) {
        auto recv_args = (PollRecvArgs *)ring_buffer_recv_args[qp_idx]->dequeue();
        recv_args->msg_handler(recv_args->fun_arg);
    }

    
    Event event(POLL_RECV, fun_args);
    event_queue->add_poll_event(event);
    return;
}

void ASTRASimGenieNetwork::sim_recv_handler(FuncArgs *fun_args) {
    #ifdef GENIE_CHROMETRACE_EVENT
    static constexpr const char* SIM_RECV_EVENT_NAME = "SIM_RECV";
    static constexpr const char* SIM_RECV_EVENT_STR = "SIM_RECV_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(SIM_RECV_EVENT_NAME, SIM_RECV_EVENT_STR, SIM_RECV);
    #endif

    auto args = static_cast<SimRecvArgs*>(fun_args);
    if (!args) {
        throw std::runtime_error("null argument to sim_recv_handler");
    }
    args->buf->recv(args->stream_id);
    int qp_idx = args->qp_idx; // Get qp_idx directly from args. SimpleRing makes it impossible to infer qp_idx from stream_id.

    PollRecvArgs *event_args = new PollRecvArgs{
        args->stream_id,
        args->qp_idx,
        args->buf,
        args->msg_handler,
        args->fun_arg
    };

    ring_buffer_recv_args[qp_idx]->enqueue(event_args);

    delete args;
    #ifdef GENIE_CHROMETRACE_EVENT
    chrome_tracer->logEventEnd(chrometrace_entry_idx);
    #endif
    return;
}
