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

#if GLOO_USE_MPI
    MPI_Init(NULL, NULL);
#endif

    int rank;
#if GLOO_USE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#else
    // Ideally this else case shouldn't even be necessary, but leaving here just in case.
    throw std::runtime_error("MPI is expected.");
#endif

    // Initialize Logger.
    AstraSim::LoggerFactory::init("empty", rank);
    std::shared_ptr<spdlog::logger> logger = AstraSim::LoggerFactory::get_logger("genie::main");

    // Parse Arguments
    ParsedArgs args;
    try {
    logger->info("Parsing Command Line Arguments");
    args = parse_arguments(argc, argv);
    logger->info("Overriding Command Line Arguments");
    read_logical_topo_config(args);
#if GLOO_USE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &args.rank);
    logger->info("  Parsed rank from MPI_COMM_WORLD: {}", args.rank);
    int mpi_num_npus;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_num_npus);
    if (args.num_npus != mpi_num_npus) {
        throw std::runtime_error(
            "Mismatch in total num npus: logical_dim: " + std::to_string(args.num_npus) +
            " mpi: " + std::to_string(mpi_num_npus));
    }
#endif

    // Print hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        logger->warn("Failed to get hostname");
    }

    logger->info("  Parsed World Size from MPI_COMM_WORLD: {}", args.num_npus);
    logger->info("  Parsed SCALE_UP_GROUP_SIZE from CMakeLists.txt: {}", SCALE_UP_GROUP_SIZE);
    logger->info("  Parsed hostname: {}", hostname);
    
    if (args.ranks_per_node > 1) {
        logger->info("  Multiple ranks per node: ");
        if (strstr(hostname, "sith") != nullptr) {
            logger->info("    Running on sith, using hardcoded rdma_driver assignment");
            std::vector<std::string> rdma_driver_array = {
                "mlx5_0", "mlx5_1", "mlx5_2", "mlx5_3",
                "mlx5_8", "mlx5_9", "mlx5_10", "mlx5_11"
            };
            args.rdma_driver = rdma_driver_array[args.rank % args.ranks_per_node];
        } else {
            logger->info("    Generic case, assign by roundrobin");
            args.rdma_driver = "mlx5_" + std::to_string(args.rank % SCALE_UP_GROUP_SIZE);
        }
    } else if (strstr(hostname, "g100n04") != nullptr) {
        logger->info("  Hardcode rdma_driver in Jakku");
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
                logger->critical("Invalid rank: {}", args.rank);
                return 1;
        }
    }
    // Initialize random seed for random functions within Gloo, that initialize
    // RDMA endpoint addresses.
    std::string rand_seed = std::to_string(std::time(nullptr)) + std::to_string(args.rank);
    std::srand(static_cast<unsigned>(std::hash<std::string>{}(rand_seed)));
    logger->info("Random set with seed {}.", rand_seed);

    logger->info("Final arguments:");
    logger->info("- workload={}", args.workload_config);
    logger->info("- system={}", args.system_config);
    logger->info("- memory={}", args.memory_config);
    logger->info("- logical_topology={}", args.logical_topology_config);
    logger->info("- rank={}", args.rank);
    logger->info("- num_npus={}", args.num_npus);
    logger->info("- ranks_per_node={}", args.ranks_per_node);
    logger->info("- rdma_driver={}", args.rdma_driver);
    logger->info("- rdma_port={}", args.rdma_port);
    logger->info("- rdma_gid_index={}", args.rdma_gid_index);
    logger->info("- comm_group={}", args.comm_group_configuration);
    logger->info("- num_qps={}", args.num_qps);
    logger->info("- num_iterations={}", args.num_iterations);

    const auto& logger_sinks = AstraSim::LoggerFactory::get_default_sinks();
    logger->info("Initializing Gloo");
    auto ibv_attr =
        gloo::transport::ibverbs::attr{args.rdma_driver, args.rdma_port, args.rdma_gid_index};
    auto dev = gloo::transport::ibverbs::CreateDevice(ibv_attr, logger_sinks);

    // Initialize context
    int nqps = args.num_qps;
    // Propagate to SimpleRing via env var so it reads the same value.
    setenv("GENIE_NUM_QPS", std::to_string(nqps).c_str(), 1);
#ifdef GLOO_USE_MPI
    // auto backingContext = std::make_shared<gloo::mpi::Context>(MPI_COMM_WORLD, IS_PINGPONG ? 2 * nqps : nqps);
    // 2x for cts packets.
    logger->info("Start to create MPI Context. Gloo will exchange RDMA Addr over MPI before connecting.");
    auto backingContext = std::make_shared<gloo::mpi::Context>(MPI_COMM_WORLD, 4 * nqps);
    logger->info("Create QPs (This step will not send RDMA traffic yet).");
    backingContext->connectFullMesh(dev);
// test_ctx->gloo_context = backingContext;
#endif

    // Ensure all ranks have completed their QP RTR/RTS transitions before
    // any rank starts sendMemoryRegion in QueuepairManager initialization.
    // Without this barrier, a fast rank can fire sendMemoryRegion to a
    // remote QP that is still in INIT state (connect() not yet called by
    // the remote), causing IBV_WC_RETRY_EXC_ERR with retry_cnt=0.
#if GLOO_USE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif



#if GLOO_USE_REDIS
    std::shared_ptr<gloo::Context> backingContext;
    int rank;
    int world_size;
    world_size = args.redis_num_ranks;  // Number of participating processes
    rank = args.rank;
    auto redis_context =
        std::make_shared<gloo::rendezvous::Context>(rank, world_size);
    logger->info("Initialize rendezvous context");
    auto redis_store =
        std::make_shared<gloo::rendezvous::RedisStore>(args.redis_ip);
    logger->info("Setup Redis Store");
    redis_context->connectFullMesh(redis_store, dev);
    logger->info("Complete full mesh");

    sleep(5);  // Sleep for 5 seconds
    if (rank == 0) {
        logger->info("Rank 0: flushing Redis store");
        redis_store->flushall();
    }
    backingContext = redis_context;
#endif


    // Default value for astra-sim struct definition.
    double comm_scale = 1;
    double injection_scale = 1;
    bool rendezvous_protocol = false;

#ifdef GENIE_CHROMETRACE_WORKLOAD
    AstraSim::ChromeTracer *chromeTracer = 
        new AstraSim::ChromeTracer(args.rank, args.num_npus);
#else
    AstraSim::ChromeTracer *chromeTracer = nullptr;
#endif
    Analytical::AnalyticalRemoteMemory* mem =
        new Analytical::AnalyticalRemoteMemory(args.memory_config);
    logger->info("Construct Genie");
    ASTRASimGenieNetwork* network =
        new ASTRASimGenieNetwork(args.rank, backingContext, chromeTracer, nqps, args.comm_group_configuration);
    AstraSim::Sys* system = new AstraSim::Sys(
        args.rank, args.num_npus, args.workload_config, args.comm_group_configuration,
        args.system_config, mem, network, args.logical_dims, args.queues_per_dim,
        injection_scale, comm_scale, rendezvous_protocol, chromeTracer);
    
    if (AstraSim::env_var_is_true("GENIE_ONLY_SCALEOUT")) {
        logger->info("GENIE_ONLY_SCALEOUT is true");
        if (args.num_npus != SCALE_UP_GROUP_SIZE) {
            logger->critical("GENIE_ONLY_SCALEOUT is true, but num_npus ({}) != SCALE_UP_GROUP_SIZE ({})", args.num_npus, SCALE_UP_GROUP_SIZE);
            throw std::runtime_error("GENIE_ONLY_SCALEOUT is true, but num_npus != SCALE_UP_GROUP_SIZE");
        }
        // We need additional checks, for example, if this is indeed truly scaleout only, but skip for now.
    }

    // Synchronization complete. START!!
    // context->getDevice()->releaseDevice();
#if GLOO_USE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    network->timekeeper->startTimer();
    for (int i = 0; i < args.num_iterations; i++) {
        system->workload->reset_for_next_iteration();
        network->event_queue->reset_for_next_iteration();

        system->workload->fire();
        network->event_queue->start();

        system->workload->finalize_iteration();
        network->event_queue->reset_for_next_iteration();
    }
    system->stat_counter->postprocess_all_streams();
    delete network;
    delete chromeTracer;

    logger->info("About to complete execution");
    
#if GLOO_USE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
    logger->info("Passed MPI barrier. All other ranks have finished.");
#endif
    // network->threadcounter->WaitThreadsJoin();
    logger->info("Program completed successfully");
    
    } catch (const std::exception& e) {
        auto err_logger = AstraSim::LoggerFactory::get_logger("genie::main");
        err_logger->critical("Exception caught. Attempting to exit gracefully: {}", e.what());
#if GLOO_USE_MPI
        // Assumption: There is no other MPI (not just barier, but all MPI call) apart from the one before firing workload.        
        // Calling MPI_Abort is not a good idea because it will kill even the good processes, preventing them from writing their verbs API traces. 
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
#endif
        return 0;
    } catch (...) {
        auto err_logger = AstraSim::LoggerFactory::get_logger("genie::main");
        err_logger->critical("Unknown exception caught");
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
