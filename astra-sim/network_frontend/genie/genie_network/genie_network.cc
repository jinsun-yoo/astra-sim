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
        auto _logger = AstraSim::LoggerFactory::get_logger("genie");
        // TODO: This assumes a ring collective of contiguous NPUs.
        int right_rank = (rank + 1 + context->size) % context->size;
        int left_rank = (rank - 1 + context->size) % context->size;
        qp_manager = new QueuepairManager(context->transportContext_, _logger, right_rank, left_rank);
        _send_lock = new std::mutex();
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
                                       AstraSim::EventType event,
                                       AstraSim::CallData* callData) {
    callable->call(event, callData);
    return;

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
                                  uint64_t message_size,
                                  int type,
                                  int dst_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    threadcounter->IncrementThreadCount();
    // We create & detatch a thread to model messages being sent in parallel to other local operations. 
    // For a description on the lifespan of the thread, refer to the comment above 'pthread_create' below.
    pthread_t thread;
    struct ThreadArgs {
        ASTRASimGenieNetwork* network;
        void (*callback_fn_ptr)(void* fun_arg);
        void* callback_fn_arg;
        int dst_id;
        uint64_t message_size;
        int send_buf_idx;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);
        ASTRASimGenieNetwork* network = threadArgs->network;
        // TODO: The buffer index and the QP is hardcoded here. 
        // int send_buf_idx = threadArgs->send_buf_idx;
        int send_buf_idx = 0;
        auto buf = network->qp_manager->send_buffers[send_buf_idx];

        // TODD: The message size is fixed to the buffer size. 
        // Must send only 'message_size' bytes.
        buf->send();        
        buf->waitSend();
        long long send_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        //threadArgs->logger->debug("Send Complete");
        threadArgs->callback_fn_ptr(threadArgs->callback_fn_arg);
        network->threadcounter->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{this, msg_handler, fun_arg, dst_id, message_size, _send_slot};
    // Slot is used to index different buffers. But for now, we assume 1-1 relation between MR(buffer) & QP. 
    // TODO: UNSAFE
    // TODO: Revive. 
    // _send_slot++;

    // Let's call the thread we create here 'thread A'. 
    // The lifespan of thread A is as follows:
    // Once the message send is completed, thread A will call the callback assigned by sim_send. This has several possibilities:
    // 1) There is another message to be sent: Thread A will eventually call sim_send once more, and will spawn a new thread, 'thread B. 
    //       Thread A will then traverse the stack up, call DecreaseThread Count, and exit. Thread B will repeat the process of sending the message, calling sim_send again if necessary, etc.
    // 2) The collective has ended: Thread A will pass the normal callstack, i.e. proceed_next_vnet -> StreamBaseline::notify_stream_finished, DataSet::notify_stream_finished -> Workload::call 
    //       This will move onto the next Chakra node (if any). Note, this implies that Chakra operations that do not have dependencies may be modelled/ran in parallel. 
    // TODO: Should we create threads somewhere else in the code?
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
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
    // TODO: Does it make sense not to create a thread here, when waitSend is in a detached thread?
    buf->waitRecv();
    long long recv_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    //logger->debug("Recv from {} to of size {} at time {}", src_id, message_size, recv_start_time);
    //logger->debug("Recv complete from {} of size {} at time {}", src_id, message_size, recv_end_time);

    msg_handler(fun_arg);
    return 0;
}
