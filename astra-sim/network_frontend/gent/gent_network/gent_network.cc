#include <thread>

#include "gent_network.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/common/Common.hh"

#include "gloo/allreduce_ring.h"

#define ENABLE_QP_POOLER

ASTRASimGentNetwork::ASTRASimGentNetwork(int rank, std::shared_ptr<gloo::Context> context)
    : AstraSim::AstraNetworkAPI(rank), _context(context), _send_slot(0), _recv_slot(0) {
        threadpooler = new Threadpooler();
        timekeeper = new Timekeeper();
        #ifdef ENABLE_QP_POOLER
        int right_rank = (rank + 1 + context->size) % context->size;
        int left_rank = (rank - 1 + context->size) % context->size;
        qp_pooler = new QueuePairPooler(context->transportContext_, right_rank, left_rank);
        #endif
        send_lock = new std::mutex();
    }

ASTRASimGentNetwork::~ASTRASimGentNetwork() {
        delete threadpooler;
    }

void ASTRASimGentNetwork::sim_all_reduce(uint64_t count) {
    return;
    /*
    int comm_size = count;
    int buffer[comm_size];
	//for (int i = 0; i < comm_size; ++i) {
	//   buffer[i] = i % 4;
	//}
    std::vector<int*> ptrs;
    ptrs.push_back(buffer);


    gloo::AllreduceRing<int> algorithm(std::static_pointer_cast<gloo::transport::Context>(_context), ptrs, comm_size);
    algorithm.run();
	auto endtime = timekeeper->elapsedTimeNanoseconds();
    //std::cout << "finished all_reduce for comm size " << comm_size << " at " << endtime <<  " nanoseconds " << std::endl;
    */
    return;
}

void ASTRASimGentNetwork::sim_notify_finished() {
    return;
}

AstraSim::timespec_t ASTRASimGentNetwork::sim_get_time() {
    AstraSim::timespec_t timeSpec;
    timeSpec.time_res = AstraSim::NS;
    timeSpec.time_val = timekeeper->elapsedTimeNanoseconds();
    return timeSpec;
}

//void ASTRASimGentNetwork::sim_schedule(AstraSim::timespec_t delta,
//                                       void (*fun_ptr)(void* fun_arg),
//                                       void* fun_arg) {
void ASTRASimGentNetwork::sim_schedule(AstraSim::timespec_t delta,
                                       AstraSim::Callable* callable,
                                       AstraSim::EventType event,
                                       AstraSim::CallData* callData) {
    //auto start_time = timekeeper->elapsedTimeNanoseconds();
    threadpooler->IncrementThreadCount();
    //auto increment_time = timekeeper->elapsedTimeNanoseconds();
    pthread_t thread;
    struct ThreadArgs {
        AstraSim::timespec_t delta;
        AstraSim::Callable* callable;
        AstraSim::EventType event;
        AstraSim::CallData* callData;
        Threadpooler* threadpooler;
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
        //std::cout << "Sim_Schedule with sleep_time " << threadArgs->delta.time_val << " and resolution " << threadArgs->delta.time_res << " start_time " << threadArgs->start_time << " increment_time " << threadArgs->increment_time << " sleep_start_time " << sleep_start_time  << " sleep_end_time " << sleep_end_time << std::endl;
        threadArgs->callable->call(threadArgs->event, threadArgs->callData);
        threadArgs->threadpooler->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{delta, callable, event, callData, threadpooler};
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return;
}


#ifdef ENABLE_QP_POOLER
int ASTRASimGentNetwork::sim_send(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int dst_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    auto logger = AstraSim::LoggerFactory::get_logger("workload");
    send_lock->lock();
    //auto start_time = timekeeper->elapsedTimeNanoseconds();
    threadpooler->IncrementThreadCount();
    pthread_t thread;
    struct ThreadArgs {
        void (*fun_ptr)(void* fun_arg);
        void* fun_arg;
        Threadpooler* threadpooler;
        std::shared_ptr<gloo::Context> context;
        int dst_id;
        uint64_t message_size;
        int slot;
        std::shared_ptr<spdlog::logger>  logger;
        QueuePairPooler* qp_pooler;
        std::mutex* send_lock;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);
        long long start_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                               .count();
        int send_buf_idx = 0;
        int reaped_send_cnt = 0;
        int queue_size = 128;
        int inflight_send_count = 0;
        while(reaped_send_cnt < NUM_BUFS) {
            //threadArgs->logger->debug("Send_idx {}, reaped_idx {}, inflight_count {}", 
            //send_buf_idx, reaped_send_cnt, inflight_send_count);
            while (send_buf_idx < NUM_BUFS && inflight_send_count < queue_size) {
                auto buf = threadArgs->qp_pooler->send_buffers[send_buf_idx];
                buf->send();        
                send_buf_idx += 1;
                inflight_send_count += 1;
            }
            //threadArgs->logger->debug("Send_idx {}, reaped_idx {}, inflight_count {}", 
            //send_buf_idx, reaped_send_cnt, inflight_send_count);
            while (reaped_send_cnt < NUM_BUFS && reaped_send_cnt < send_buf_idx) {
                auto buf = threadArgs->qp_pooler->send_buffers[reaped_send_cnt];
                buf->waitSend();
                reaped_send_cnt += 1;
                inflight_send_count -= 1;
            }
            //threadArgs->logger->debug("Send_idx {}, reaped_idx {}, inflight_count {}", 
            //send_buf_idx, reaped_send_cnt, inflight_send_count);
        }
        long long send_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        threadArgs->logger->debug("Send Complete");
        threadArgs->send_lock->unlock();
        threadArgs->fun_ptr(threadArgs->fun_arg);
        threadArgs->threadpooler->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    //ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, dst_id, message_size, _send_slot, start_time, increment_time, timekeeper};
    ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, dst_id, message_size, _send_slot, logger, qp_pooler, send_lock};
    // TODO: UNSAFE
    _send_slot++;
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return 0;
}

int ASTRASimGentNetwork::sim_recv(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int src_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    auto logger = AstraSim::LoggerFactory::get_logger("Gent");
    threadpooler->IncrementThreadCount();
    pthread_t thread;
    struct ThreadArgs {
        void (*fun_ptr)(void* fun_arg);
        void* fun_arg;
        Threadpooler* threadpooler;
        std::shared_ptr<gloo::Context> context;
        int src_id;
        uint64_t message_size;
        int slot;
        std::shared_ptr<spdlog::logger>  logger;
        QueuePairPooler* qp_pooler; 
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);

        long long recv_start_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        for (int idx = 0; idx < NUM_BUFS; idx++) {
            auto buf = threadArgs->qp_pooler->recv_buffers[idx];
            buf->waitRecv();
            //threadArgs->logger->debug("Recv idx {} to of size {} at time {}", idx, ////threadArgs->message_size, recv_start_time);
        }
        //std::cout << "Complete recv" << std::endl;
        long long recv_end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        threadArgs->logger->debug("Recv from {} to of size {} at time {}", threadArgs->src_id, threadArgs->message_size, recv_start_time);
        threadArgs->logger->debug("Recv complete from {} of size {} at time {}", threadArgs->src_id, threadArgs->message_size, recv_end_time);

        threadArgs->fun_ptr(threadArgs->fun_arg);
        threadArgs->threadpooler->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, src_id, message_size, _recv_slot, logger, qp_pooler};
    // TODO: UNSAFE
    _recv_slot++;
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return 0;
}

#else

int ASTRASimGentNetwork::sim_send(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int dst_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    auto logger = AstraSim::LoggerFactory::get_logger("workload");
    logger->debug("Send from {} to {} of size {}", rank, dst_id, message_size);
    //auto start_time = timekeeper->elapsedTimeNanoseconds();
    threadpooler->IncrementThreadCount();
    //auto increment_time = timekeeper->elapsedTimeNanoseconds();
    pthread_t thread;
    struct ThreadArgs {
        void (*fun_ptr)(void* fun_arg);
        void* fun_arg;
        Threadpooler* threadpooler;
        std::shared_ptr<gloo::Context> context;
        int dst_id;
        uint64_t message_size;
        int slot;
        std::shared_ptr<spdlog::logger>  logger;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);

        //auto thrd_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        int *data = new int[threadArgs->message_size / 4];
        const auto& pair = threadArgs->context->getPair(threadArgs->dst_id);
        const auto& buf = pair->createSendBuffer(threadArgs->slot, data, threadArgs->message_size);
        //auto buf_end_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        //std::cout << "Create send buffer to: " << threadArgs->dst_id << " size: " << threadArgs->message_size  << " slot_id " << threadArgs->slot <<  std::endl;
        buf->send();        
        //auto send_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        buf->waitSend();
        //auto send_wait_time = threadArgs->timekeeper->elapsedTimeNanoseconds();
        //std::cout << "Sim_Send start_time: " << threadArgs->start_time << " increment_time: " << threadArgs->increment_time << " thread_time: " << thrd_time << " buf_end_time: " << buf_end_time << " send_time: " << send_time << " send_wait_time: " << send_wait_time << std::endl;
        //std::cout << "Complete send" << std::endl;
        threadArgs->logger->debug("Send complete to {} of size {}", threadArgs->dst_id, threadArgs->message_size);

        threadArgs->fun_ptr(threadArgs->fun_arg);
        threadArgs->threadpooler->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    //ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, dst_id, message_size, _send_slot, start_time, increment_time, timekeeper};
    ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, dst_id, message_size, _send_slot, logger};
    // TODO: UNSAFE
    _send_slot++;
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return 0;
}

int ASTRASimGentNetwork::sim_recv(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int src_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    auto logger = AstraSim::LoggerFactory::get_logger("Gent");
    logger->debug("Recv from {} to {} of size {}", src_id, rank, message_size);
    threadpooler->IncrementThreadCount();
    pthread_t thread;
    struct ThreadArgs {
        void (*fun_ptr)(void* fun_arg);
        void* fun_arg;
        Threadpooler* threadpooler;
        std::shared_ptr<gloo::Context> context;
        int src_id;
        uint64_t message_size;
        int slot;
        std::shared_ptr<spdlog::logger>  logger;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);

        int *recvBuf = new int[threadArgs->message_size / 4];
        const auto&pair = threadArgs->context->getPair(threadArgs->src_id);
        auto buf = pair->createRecvBuffer(threadArgs->slot, recvBuf, threadArgs->message_size);
        //std::cout << "Create recv buffer from: " << threadArgs->src_id << " size: " << threadArgs->message_size << " slot_id " << threadArgs->slot << std::endl;
        buf->waitRecv();
        //std::cout << "Complete recv" << std::endl;

        threadArgs->logger->debug("Recv complete from {} of size {}", threadArgs->src_id, threadArgs->message_size);
        threadArgs->fun_ptr(threadArgs->fun_arg);
        threadArgs->threadpooler->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, src_id, message_size, _recv_slot, logger};
    // TODO: UNSAFE
    _recv_slot++;
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return 0;
}

#endif
