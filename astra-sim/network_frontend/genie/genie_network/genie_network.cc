#include <thread>

#include "genie_network.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/WorkloadLayerHandlerData.hh"
#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/common/Common.hh"
#include <chrono>
#include <x86intrin.h>

static inline uint64_t rdtscp_intrinsic(void) {
    unsigned int aux;
    return __rdtscp(&aux);
}

ASTRASimGenieNetwork::ASTRASimGenieNetwork(int rank, std::shared_ptr<gloo::Context> context, AstraSim::ChromeTracer* chrome_tracer)
    : AstraSim::AstraNetworkAPI(rank), _context(context), _send_slot(0), _recv_slot(0), chrome_tracer(chrome_tracer) {
        threadcounter = new Threadcounter();
        timekeeper = new Timekeeper();
        _logger = AstraSim::LoggerFactory::get_logger("genie");
        // TODO: This assumes a ring collective of contiguous NPUs.
        int right_rank = (rank + 1 + context->size) % context->size;
        int left_rank = (rank - 1 + context->size) % context->size;
        qp_manager = new QueuepairManager(context->transportContext_, _logger, right_rank, left_rank);
        _send_lock = new std::mutex();
        event_queue = new EventQueue(this);
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
    int send_buf_idx = 0;
    auto buf = qp_manager->send_buffers[send_buf_idx];

    SimSendArgs *event_args = new SimSendArgs{
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
    auto buf = qp_manager->recv_buffers[0];
    auto event_args = new SimRecvArgs {
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
    std::string event_name = std::to_string(args->event_id);
    EventType event_type = SCHEDULE_EVENT;
    std::string event_str = "SCHEDULE_EVENT";
    int chrometrace_entry_idx = chrome_tracer->logEventStart(event_name, event_str, event_type);

    auto current_time = std::chrono::steady_clock::now();
    auto duration = current_time - args->start_time;
    long double duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    if (args->delta.time_val > duration_ns) {
        // Still need to wait more
        Event event(SCHEDULE_EVENT, args);
        bool did_sleep = event_queue->add_poll_event(event);
        chrome_tracer->logEventEnd(chrometrace_entry_idx);
        return;
    }
    args->callable->call(args->event, args->call_data);
    chrome_tracer->logEventEnd(chrometrace_entry_idx, true);
    delete args;
}

void ASTRASimGenieNetwork::poll_send_handler(void *fun_arg) {
    // _logger->info("Poll_send called");
    int chrometrace_entry_idx = chrome_tracer->logEventStart("POLL_SEND", "POLL_SEND_EVENT", POLL_SEND);
    PollSendArgs *args = static_cast<PollSendArgs*>(fun_arg);
    if (!args) {
        throw std::runtime_error("null argument to poll_send_handler");
    }

    auto sendComplete = args->buf->pollSend();
    bool did_sleep = false;
    if (sendComplete) {
        // _logger->info("Poll_send return complete");
        chrome_tracer->logEventEnd(chrometrace_entry_idx, true);
        args->msg_handler(args->fun_arg);
        // _logger->info("Poll_send exit");
        // The callback handler for sim_send is always nullptr.
        delete args;
    } else {
        // _logger->info("poll_send return not complete");
        Event event(POLL_SEND, fun_arg);
        did_sleep = event_queue->add_poll_event(event);
        chrome_tracer->logEventEnd(chrometrace_entry_idx);
    }
    return;
}

void ASTRASimGenieNetwork::sim_send_handler(void *fun_arg) {
    int chrometrace_entry_idx = chrome_tracer->logEventStart("SIM_SEND", "SIM_SEND", SIM_SEND);
    SimSendArgs *args = static_cast<SimSendArgs*>(fun_arg);
    if (!args) {
        throw std::runtime_error("null argument to sim_send_handler");
    }

    args->buf->send(0, args->msg_size);

    PollSendArgs *event_args = new PollSendArgs{
        args->buf,
        args->msg_handler,
        args->fun_arg
    };
    Event event(POLL_SEND, event_args);
    args->event_queue->add_event(event);
    // std::cout << "sim_send called" << std::endl;

    delete args;
    chrome_tracer->logEventEnd(chrometrace_entry_idx);
    return;
}

void ASTRASimGenieNetwork::poll_recv_handler(void *fun_args) {
    uint64_t logstartstart = 0, logstartend = 0, logpollend = 0, logcompleteend = 0, logendend = 0, msghandlerend = 0, eventend = 0, addpollend = 0;
    
    logstartstart = rdtscp_intrinsic();

    int chrometrace_entry_idx = chrome_tracer->logEventStart("POLL_RECV", "POLL_RECV_EVENT", POLL_RECV);
    auto args = static_cast<PollRecvArgs*>(fun_args);

    logstartend = rdtscp_intrinsic();

    auto recvComplete = args->buf->pollRecv();

    logpollend = rdtscp_intrinsic();
    bool did_sleep = false;
    if (recvComplete) {
        if (!args->msg_handler) {
            throw std::runtime_error("No message handler in poll_recv_handler");
        }
        chrome_tracer->logEventEnd(chrometrace_entry_idx, true);
        logcompleteend = rdtscp_intrinsic();
        args->msg_handler(args->fun_arg);
        msghandlerend = rdtscp_intrinsic(); 
        // The callback handler for sim_send is always nullptr.
        delete args;
    } else {
        //_logger->info("poll_recv return not complete");
        Event event(POLL_RECV, fun_args);
        eventend = rdtscp_intrinsic();

        did_sleep = event_queue->add_poll_event(event);

        addpollend = rdtscp_intrinsic();

        chrome_tracer->logEventEnd(chrometrace_entry_idx);

        logendend = rdtscp_intrinsic();
    }

    uint64_t logstartdur = logstartend - logstartstart;
    uint64_t polldur = logpollend - logstartend;
    uint64_t logcompleteenddur = 0, msghandlerdur = 0, eventconstrdur = 0, addpolldur = 0, logenddur = 0;
    if (recvComplete){
    logcompleteenddur = logcompleteend - logpollend;
    msghandlerdur = msghandlerend - logcompleteend;
    } else {
    eventconstrdur = eventend - logpollend;
    addpolldur = addpollend - eventend;
    logenddur = logendend - addpollend;
    }

    chrome_tracer->logpollrecv(logstartdur, polldur, logcompleteenddur, msghandlerdur, eventconstrdur, addpolldur, logenddur);
    return;
}

void ASTRASimGenieNetwork::sim_recv_handler(void *fun_args) {
    int chrometrace_entry_idx = chrome_tracer->logEventStart("SIM_RECV", "SIM_RECV", SIM_RECV);
    // std::cout << "sim_recv start " << std::endl;
    auto args = static_cast<SimRecvArgs*>(fun_args);
    if (!args) {
        throw std::runtime_error("null argument to sim_recv_handler");
    }

    PollRecvArgs *event_args = new PollRecvArgs{
        args->buf,
        args->msg_handler,
        args->fun_arg,
    };
    Event event(POLL_RECV, event_args);
    args->event_queue->add_event(event);

    delete args;
    chrome_tracer->logEventEnd(chrometrace_entry_idx);
    // std::cout << "sim_recv end" << std::endl;
    return;
}
