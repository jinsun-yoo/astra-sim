#include <thread>

#include "gent_network.hh"
#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/common/Common.hh"

#include "gloo/allreduce_ring.h"

ASTRASimGentNetwork::ASTRASimGentNetwork(int rank, std::shared_ptr<gloo::rendezvous::Context> context)
    : AstraSim::AstraNetworkAPI(rank), _context(context), timekeeper(), _send_slot(0), _recv_slot(0) {
        threadpooler = new Threadpooler();
    }

ASTRASimGentNetwork::~ASTRASimGentNetwork() {
        delete threadpooler;
    }

void ASTRASimGentNetwork::sim_all_reduce(uint64_t count) {
    int comm_size = count;
    int buffer[comm_size];
	//for (int i = 0; i < comm_size; ++i) {
	//   buffer[i] = i % 4;
	//}
    std::vector<int*> ptrs;
    ptrs.push_back(buffer);

    gloo::AllreduceRing<int> algorithm(_context, ptrs, comm_size);
    algorithm.run();
	auto endtime = timekeeper.elapsedTimeNanoseconds();
    std::cout << "finished all_reduce for comm size " << comm_size << " at " << endtime <<  " nanoseconds " << std::endl;
    return;
}

void ASTRASimGentNetwork::sim_notify_finished() {
    return;
}

AstraSim::timespec_t ASTRASimGentNetwork::sim_get_time() {
    AstraSim::timespec_t timeSpec;
    timeSpec.time_res = AstraSim::NS;
    timeSpec.time_val = timekeeper.elapsedTimeNanoseconds();
    return timeSpec;
}

//void ASTRASimGentNetwork::sim_schedule(AstraSim::timespec_t delta,
//                                       void (*fun_ptr)(void* fun_arg),
//                                       void* fun_arg) {
void ASTRASimGentNetwork::sim_schedule(AstraSim::timespec_t delta,
                                       AstraSim::Callable* callable,
                                       AstraSim::EventType event,
                                       AstraSim::CallData* callData) {
    threadpooler->IncrementThreadCount();
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
        if (threadArgs->delta.time_res == AstraSim::NS) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        } else if (threadArgs->delta.time_res == AstraSim::US) {
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        } else if (threadArgs->delta.time_res == AstraSim::MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(threadArgs->delta.time_val)));
        }

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

int ASTRASimGentNetwork::sim_send(void* buffer,
                                  uint64_t message_size,
                                  int type,
                                  int dst_id,
                                  int tag,
                                  AstraSim::sim_request* request,
                                  void (*msg_handler)(void* fun_arg),
                                  void* fun_arg) {
    threadpooler->IncrementThreadCount();
    pthread_t thread;
    struct ThreadArgs {
        void (*fun_ptr)(void* fun_arg);
        void* fun_arg;
        Threadpooler* threadpooler;
        std::shared_ptr<gloo::rendezvous::Context> context;
        int dst_id;
        uint64_t message_size;
        int slot;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);

        int *data = new int[threadArgs->message_size / 4];
        const auto& pair = threadArgs->context->getPair(threadArgs->dst_id);
        const auto& buf = pair->createSendBuffer(threadArgs->slot, data, threadArgs->message_size);
        std::cout << "Create send buffer to: " << threadArgs->dst_id << " size: " << threadArgs->message_size  << " slot_id " << threadArgs->slot <<  std::endl;
        buf->send();        
        buf->waitSend();
        std::cout << "Complete send" << std::endl;

        threadArgs->fun_ptr(threadArgs->fun_arg);
        threadArgs->threadpooler->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, dst_id, message_size, _send_slot};
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
    threadpooler->IncrementThreadCount();
    pthread_t thread;
    struct ThreadArgs {
        void (*fun_ptr)(void* fun_arg);
        void* fun_arg;
        Threadpooler* threadpooler;
        std::shared_ptr<gloo::rendezvous::Context> context;
        int src_id;
        uint64_t message_size;
        int slot;
    };

    auto thread_func = [](void* args) -> void* {
        ThreadArgs* threadArgs = static_cast<ThreadArgs*>(args);

        int *recvBuf = new int[threadArgs->message_size / 4];
        const auto&pair = threadArgs->context->getPair(threadArgs->src_id);
        auto buf = pair->createRecvBuffer(threadArgs->slot, recvBuf, threadArgs->message_size);
        std::cout << "Create recv buffer from: " << threadArgs->src_id << " size: " << threadArgs->message_size << " slot_id " << threadArgs->slot << std::endl;
        buf->waitRecv();
        std::cout << "Complete recv" << std::endl;

        threadArgs->fun_ptr(threadArgs->fun_arg);
        threadArgs->threadpooler->DecreaseThreadCount();
        delete threadArgs;
        return nullptr;
    };

    ThreadArgs* args = new ThreadArgs{msg_handler, fun_arg, threadpooler, _context, src_id, message_size, _recv_slot};
    // TODO: UNSAFE
    _recv_slot++;
    pthread_create(&thread, nullptr, thread_func, args);
    pthread_detach(thread);
    return 0;
}
