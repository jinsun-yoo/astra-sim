#include <getopt.h>
#include <unistd.h>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/common/ChromeTracer.hh"
#include "astra-sim/system/Sys.hh"
#include "extern/remote_memory_backend/analytical/AnalyticalRemoteMemory.hh"
#include "genie_network/genie_network.hh"
#include "args.hh"

#include <gloo/config.h>
#include <gloo/rendezvous/context.h>
#include <gloo/transport/ibverbs/pair.h>

#if GLOO_USE_REDIS
#include <gloo/rendezvous/redis_store.h>
#endif

#if GLOO_USE_MPI
#include <gloo/mpi/context.h>
#include <mpi.h>
#endif

#if GLOO_USE_FILESTORE
#include <gloo/rendezvous/file_store.h>
#endif

#define DEBUG_SIGSEV_SIGINT 0
#if DEBUG_SIGSEGV_SIGINT
#include <signal.h>
#include <cstring>

void handler(int sig, siginfo_t* info, void *ucontext) {
    write(STDERR_FILENO, "Segfault caught\n", 16);

    const char* gdb_debug_env = getenv("GDB_DEBUG");
    if (gdb_debug_env != nullptr) {
        std::cout << "GDB_DEBUG=" << gdb_debug_env << std::endl;
        sleep(300);
    }
    exit(1);
}
void sigint_handler(int sig, siginfo_t* info, void *ucontext) {
    write(STDERR_FILENO, "Signint caught\n", 16);
    MPI_Finalize();
    exit(0);
}
#endif

int main(int argc, char* argv[]) {
    #if DEBUG_SIGSEV_SIGINT
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    
    struct sigaction sa_int;
    std::memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_sigaction = sigint_handler;
    sa_int.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &sa_int, nullptr);
    #endif

    // Flush output immediately for debugging
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    
#if GLOO_USE_MPI
    MPI_Init(NULL, NULL);
#endif

    ParsedArgs args = parse_arguments(argc, argv);
#if GLOO_USE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &args.rank);
    std::cout << "Parsed Rank from MPI_COMM_WORLD: " << args.rank << std::endl;
#endif
    try {

    // Initialize Gloo
    std::cout << "Hello, world!" << std::endl;
    
    // Print hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        std::cout << "Running on hostname: " << hostname << std::endl;
    } else {
        std::cout << "Failed to get hostname" << std::endl;
    }
    
    if (std::getenv("GENIE_RUN_SCALEUP") != nullptr) {
        args.rdma_driver = "mlx5_" + std::to_string(args.rank % 8);
        std::cout << "GENIE_RUN_SCALEUP is set, overriding rdma_driver to "
                  << args.rdma_driver << std::endl;
    } else if (strstr(hostname, "g100n040") != nullptr) {
        std::cout << "Hardcode rdma_driver in vader" << std::endl;
        switch(args.rank) {
            case 0:
            case 2:
                args.rdma_driver = "mlx5_0";
                break;
            case 1:
            case 3:
                args.rdma_driver = "mlx5_7";
                break;
            default:
                std::cerr << "Invalid rank: " << args.rank << std::endl;
                return 1;
        }
    }
    // Device name obtained by running 'rdma dev' on command line
    // Port from 'rdma link'
    auto ibv_attr =
        gloo::transport::ibverbs::attr{args.rdma_driver, args.rdma_port, args.rdma_gid_index};
    std::cout << "Initialize ibv attr" << std::endl;
    auto dev = gloo::transport::ibverbs::CreateDevice(ibv_attr);
    std::cout << "Initialize ibv dev" << std::endl;

    // Initialize context
    int nqps = NUM_QPS; 
#ifdef GLOO_USE_MPI
    // auto backingContext = std::make_shared<gloo::mpi::Context>(MPI_COMM_WORLD, IS_PINGPONG ? 2 * nqps : nqps);
    // 2x for cts packets.
    auto backingContext = std::make_shared<gloo::mpi::Context>(MPI_COMM_WORLD, 4 * nqps);
    std::cout << "Created mpi context" << std::endl;
    backingContext->connectFullMesh(dev);
    std::cout << "Connected mesh " << std::endl;
// test_ctx->gloo_context = backingContext;
#endif
    std::cout << "Established Connection!" << std::endl;



#if GLOO_USE_REDIS
    std::shared_ptr<gloo::Context> backingContext;
    int rank;
    int world_size;
    world_size = args.redis_num_ranks;  // Number of participating processes
    rank = args.rank;
    auto redis_context =
        std::make_shared<gloo::rendezvous::Context>(rank, world_size);
    std::cout << "Initialize rendezvous context" << std::endl;
    auto redis_store =
        std::make_shared<gloo::rendezvous::RedisStore>(args.redis_ip);
    std::cout << "Setup Redis Store" << std::endl;
    redis_context->connectFullMesh(redis_store, dev);
    std::cout << "Complete full mesh" << std::endl;

    sleep(5);  // Sleep for 5 seconds
    if (rank == 0) {
        std::cout << "Rank 0: flushing Redis store" << std::endl;
        redis_store->flushall();
    }
    backingContext = redis_context;
#endif

    // Initialize random seed for random functions within Gloo, that initialize
    // RDMA endpoint addresses.
    std::srand(static_cast<unsigned>(std::hash<std::string>{}(
        std::to_string(std::time(nullptr)) + std::to_string(args.rank))));
    std::cout << "Random seed initialized" << std::endl;

    // Default value for astra-sim struct definition.
    double comm_scale = 1;
    double injection_scale = 1;
    bool rendezvous_protocol = false;

    read_logical_topo_config(args);
    AstraSim::LoggerFactory::init(args.logging_configuration, args.rank);
    // TODO: CONSOLIDATE BTWN MPI COMM AND LOGICAL TOPO
    MPI_Comm_size(MPI_COMM_WORLD, &args.num_npus);
    std::cout << "Parsed World Size from MPI_COMM_WORLD: " << args.num_npus << std::endl;
#ifdef GENIE_CHROMETRACE_WORKLOAD
    AstraSim::ChromeTracer *chromeTracer = 
        new AstraSim::ChromeTracer(args.rank, args.num_npus);
#else
    AstraSim::ChromeTracer *chromeTracer = nullptr;
#endif
    Analytical::AnalyticalRemoteMemory* mem =
        new Analytical::AnalyticalRemoteMemory(args.memory_config);
    ASTRASimGenieNetwork* network =
        new ASTRASimGenieNetwork(args.rank, backingContext, chromeTracer, nqps, args.comm_group_configuration);
    AstraSim::Sys* system = new AstraSim::Sys(
        args.rank, args.num_npus, args.workload_config, args.comm_group_configuration,
        args.system_config, mem, network, args.logical_dims, args.queues_per_dim,
        injection_scale, comm_scale, rendezvous_protocol, chromeTracer);

    // Synchronization complete. START!!
    // context->getDevice()->releaseDevice();
#if GLOO_USE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    network->timekeeper->startTimer();
    system->workload->fire();
    network->event_queue->start();
    system->stat_counter->postprocess_all_streams();
    delete network;
    delete chromeTracer;

    std::cout << "Rank " << args.rank << ": About to complete execution" << std::endl;
    
#if GLOO_USE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
    std::cout << "Rank " << args.rank << ": Passed MPI barrier" << std::endl;
#endif
    // network->threadcounter->WaitThreadsJoin();
    std::cout << "Program completed successfully" << std::endl;
    
    } catch (const std::exception& e) {
        std::cerr << "Exception caught at rank " << args.rank << ": " << e.what() << std::endl;
#if GLOO_USE_MPI
        MPI_Finalize();
#endif
        return 0;
    } catch (...) {
        std::cerr << "Unknown exception caught at rank " << args.rank << std::endl;
#if GLOO_USE_MPI
        MPI_Finalize();
#endif
        return 0;
    }
    
#if GLOO_USE_MPI
    MPI_Finalize();
#endif
    return 0;
}
