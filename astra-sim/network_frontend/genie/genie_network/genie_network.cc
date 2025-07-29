#include <thread>

#include "genie_network.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/common/Common.hh"

ASTRASimGenieNetwork::ASTRASimGenieNetwork(int rank, std::shared_ptr<gloo::Context> context)
    : AstraSim::AstraNetworkAPI(rank), _context(context), _send_slot(0), _recv_slot(0) {
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
        delete threadcounter;
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
    SimScheduleArgs *event_args = new SimScheduleArgs {
        delta,
        callable,
        event_type,
        callData,
        start_time
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

    auto current_time = std::chrono::steady_clock::now();
    auto duration = current_time - args->start_time;
    long double duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    if (args->delta.time_val > duration_ns) {
        // Still need to wait more
        Event event(SCHEDULE_EVENT, args);
        event_queue->add_poll_event(event);
        return;
    }
    args->callable->call(args->event, args->call_data);
    delete args;
}

void ASTRASimGenieNetwork::poll_send_handler(void *fun_arg) {
    // _logger->info("Poll_send called");
    PollSendArgs *args = static_cast<PollSendArgs*>(fun_arg);
    if (!args) {
        throw std::runtime_error("null argument to poll_send_handler");
    }

    auto sendComplete = args->buf->pollSend();
    if (sendComplete) {
        // _logger->info("Poll_send return complete");
        args->msg_handler(args->fun_arg);
        // _logger->info("Poll_send exit");
        // The callback handler for sim_send is always nullptr.
        delete args;
    } else {
        // _logger->info("poll_send return not complete");
        Event event(POLL_SEND, fun_arg);
        event_queue->add_poll_event(event);
    }
    return;
}

void ASTRASimGenieNetwork::sim_send_handler(void *fun_arg) {
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
    return;
}

void ASTRASimGenieNetwork::poll_recv_handler(void *fun_args) {
    auto args = static_cast<PollRecvArgs*>(fun_args);

    // std::cout << "poll_recv start" << std::endl;
    auto recvComplete = args->buf->pollRecv();
    if (recvComplete) {
        //_logger->info("poll_recv return complete");
        if (!args->msg_handler) {
            throw std::runtime_error("No message handler in poll_recv_handler");
        }
        args->msg_handler(args->fun_arg);
        //_logger->info("Poll_send exit");
        // The callback handler for sim_send is always nullptr.
        delete args;
    } else {
        //_logger->info("poll_recv return not complete");
        Event event(POLL_RECV, fun_args);
        event_queue->add_poll_event(event);
    }
    return;
}

void ASTRASimGenieNetwork::sim_recv_handler(void *fun_args) {
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
    // std::cout << "sim_recv end" << std::endl;
    return;
}
