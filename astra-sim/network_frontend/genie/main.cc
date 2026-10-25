#include <cstdlib>
#include <getopt.h>
#include <sched.h>
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

#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifndef GENIE_BARRIER_TIMEOUT_S
#define GENIE_BARRIER_TIMEOUT_S 120
#endif

// Ranks in disjoint scale-out domains (e.g. one FSDP group per TP index when TP is
// simulated analytically) are not coupled, so a healthy run can finish minutes apart
// on hosts with a slower NIC. The final barrier therefore waits longer (the job-level
// GENIE_TIMEOUT still bounds real hangs); override with env GENIE_FINAL_BARRIER_TIMEOUT_S.
#ifndef GENIE_FINAL_BARRIER_TIMEOUT_S
#define GENIE_FINAL_BARRIER_TIMEOUT_S 1800
#endif

static int final_barrier_timeout_s() {
    const char* env = std::getenv("GENIE_FINAL_BARRIER_TIMEOUT_S");
    if (env != nullptr && std::atoi(env) > 0) return std::atoi(env);
    return GENIE_FINAL_BARRIER_TIMEOUT_S;
}

#if GLOO_USE_MPI
// MPI_Barrier with a deadline: returns false if not all ranks arrived within
// timeout_s seconds (the pending barrier is then abandoned).
static bool barrier_with_timeout(int timeout_s = GENIE_BARRIER_TIMEOUT_S) {
    MPI_Request req;
    MPI_Ibarrier(MPI_COMM_WORLD, &req);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    int done = 0;
    while (true) {
        MPI_Test(&req, &done, MPI_STATUS_IGNORE);
        if (done) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
#endif

// Champollion compute nodes are g100n050-g100n069 (HPE XL675d, 8x A100,
// 4x ConnectX-6 HDR). Jakku (g100n04x) and other g100n hosts are excluded.
static bool is_champollion_host(const char* hostname) {
    int node = -1;
    char rest = '\0';
    int matched = std::sscanf(hostname, "g100n%3d%c", &node, &rest);
    return matched >= 1 && (matched == 1 || rest == '.') && node >= 50 && node <= 69;
}

// Returns the RDMA device (e.g. mlx5_0) whose PCI function is `pci_bdf`, or "".
static std::string ib_device_for_pci(const std::string& pci_bdf) {
    const std::string base = "/sys/class/infiniband";
    DIR* dir = opendir(base.c_str());
    if (dir == nullptr) return "";
    std::string found;
    while (struct dirent* ent = readdir(dir)) {
        if (ent->d_name[0] == '.') continue;
        char resolved[PATH_MAX];
        std::string link = base + "/" + ent->d_name + "/device";
        if (realpath(link.c_str(), resolved) == nullptr) continue;
        const char* leaf = std::strrchr(resolved, '/');
        if (leaf != nullptr && pci_bdf == leaf + 1) {
            found = ent->d_name;
            break;
        }
    }
    closedir(dir);
    return found;
}

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
    // Declared here (outside the try block) so that it is still in scope, and can be explicitly deleted
    // to flush its buffered trace events to disk, in the catch blocks below if an exception is thrown.
    AstraSim::ChromeTracer *chromeTracer = nullptr;
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
    logger->info("  Running on CPU core {} at program start", sched_getcpu());
    
    // A launcher can choose an exact device per local rank. Do not replace it
    // with the legacy hostname/global-rank-based defaults below.
    const char* rdma_driver_override = std::getenv("GENIE_RDMA_DRIVER");
    if (rdma_driver_override != nullptr && rdma_driver_override[0] != '\0') {
        args.rdma_driver = rdma_driver_override;
        logger->info("  Explicit GENIE_RDMA_DRIVER: {}", args.rdma_driver);
    } else if (args.ranks_per_node > 1) {
        logger->info("  Multiple ranks per node: ");
        if (is_champollion_host(hostname)) {
            // Logical NIC per local rank (GPU pairs 0/1, 2/3, 4/5, 6/7 share a
            // PCIe switch with NIC 0/1/2/3). NICs are resolved by PCI address
            // (outputs/210444/hw/*/lspci_tree.txt), not by mlx5_N name.
            static const std::vector<std::string> nic_pci = {
                "0000:0e:00.0", "0000:45:00.0", "0000:85:00.0", "0000:c5:00.0"};
            static const std::map<int, std::vector<int>> nic_by_rpn = {
                {2, {0, 1}}, {4, {0, 1, 2, 3}}, {8, {0, 0, 1, 1, 2, 2, 3, 3}}};
            const char* local_rank_env = std::getenv("OMPI_COMM_WORLD_LOCAL_RANK");
            const char* local_size_env = std::getenv("OMPI_COMM_WORLD_LOCAL_SIZE");
            auto layout = nic_by_rpn.find(args.ranks_per_node);
            if (local_rank_env == nullptr || local_size_env == nullptr || layout == nic_by_rpn.end() ||
                std::atoi(local_size_env) != args.ranks_per_node) {
                throw std::runtime_error(
                    "Champollion NIC mapping needs Open MPI local rank/size equal to --ranks_per_node "
                    "in {2,4,8}; otherwise set GENIE_RDMA_DRIVER per rank");
            }
            int local_rank = std::atoi(local_rank_env);
            if (local_rank < 0 || local_rank >= args.ranks_per_node) {
                throw std::runtime_error("Invalid OMPI_COMM_WORLD_LOCAL_RANK: " + std::string(local_rank_env));
            }
            const std::string& pci = nic_pci[layout->second[local_rank]];
            args.rdma_driver = ib_device_for_pci(pci);
            if (args.rdma_driver.empty()) {
                throw std::runtime_error("No RDMA device at PCI " + pci + " on " + hostname);
            }
            logger->info("    Champollion local rank {} -> logical NIC {} (PCI {})",
                         local_rank, layout->second[local_rank], pci);
        } else if (strstr(hostname, "sith") != nullptr) {
            logger->info("    Running on sith, using hardcoded rdma_driver assignment");
            std::vector<std::string> rdma_driver_array = {
                "mlx5_0", "mlx5_2", "mlx5_8", "mlx5_10", // Both numa 0, numa 1
                "mlx5_1", "mlx5_3", "mlx5_9", "mlx5_11" // Both numa 0, numa 1
            };
            if (strstr(hostname, "sith1") != nullptr) {
                logger->info("    Sith1 has a different set of netdevs");
                rdma_driver_array = {
                    "mlx5_0", "mlx5_2", "mlx5_6", "mlx5_8", // Both numa 0, numa 1
                    "mlx5_1", "mlx5_3", "mlx5_7", "mlx5_9" // Both numa 0, numa 1
                };
            }
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
#ifdef REPLAY_CPU_ONLY
    logger->info("REPLAY_CPU_ONLY is true, so override rdma_driver to be empty");
    args.rdma_driver = "";
#endif

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

    logger->info("Initializing Gloo");
#ifndef REPLAY_CPU_ONLY
    const auto& logger_sinks = AstraSim::LoggerFactory::get_default_sinks();
    auto ibv_attr =
        gloo::transport::ibverbs::attr{args.rdma_driver, args.rdma_port, args.rdma_gid_index};
    auto dev = gloo::transport::ibverbs::CreateDevice(ibv_attr, logger_sinks);
#endif

    // Initialize context
    int nqps = args.num_qps;
    // Propagate to SimpleRing via env var so it reads the same value.
    setenv("GENIE_NUM_QPS", std::to_string(nqps).c_str(), 1);
#ifdef GLOO_USE_MPI
    // auto backingContext = std::make_shared<gloo::mpi::Context>(MPI_COMM_WORLD, IS_PINGPONG ? 2 * nqps : nqps);
    // 2x for cts packets.
    logger->info("Start to create MPI Context. Gloo will exchange RDMA Addr over MPI before connecting.");
    // One set of 4 * nqps pairs per scale-out member set sharing a rank pair (see GeniePairChannelLayout).
    int mpi_world_size = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_world_size);
    const int pair_channels =
        genie_pair_channel_layout(args.comm_group_configuration, mpi_world_size).num_channels;
    logger->info("- gloo pair channels per rank pair={} ({} pairs per peer)", pair_channels, 4 * nqps * pair_channels);
    auto backingContext = std::make_shared<gloo::mpi::Context>(MPI_COMM_WORLD, 4 * nqps * pair_channels);
#ifndef REPLAY_CPU_ONLY
    logger->info("Create QPs (This step will not send RDMA traffic yet).");
    backingContext->connectFullMesh(dev);
#endif
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
    chromeTracer = new AstraSim::ChromeTracer(args.rank, args.num_npus);
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
    // network->assert_no_pending_operations(i);

        system->workload->finalize_iteration();
        network->event_queue->reset_for_next_iteration();
    }
    system->stat_counter->postprocess_all_streams();
    delete network;
    delete chromeTracer;

    logger->info("About to complete execution");
    
#if GLOO_USE_MPI
    const int final_timeout_s = final_barrier_timeout_s();
    if (!barrier_with_timeout(final_timeout_s)) {
        logger->critical("Timed out after {}s waiting at the final MPI barrier; other ranks did not finish", final_timeout_s);
        return 1;
    }
    logger->info("Passed MPI barrier. All other ranks have finished.");
#endif
    // network->threadcounter->WaitThreadsJoin();
    logger->info("Running on CPU core {} at program end", sched_getcpu());
    logger->info("Program completed successfully");
    
    } catch (const std::exception& e) {
        auto err_logger = AstraSim::LoggerFactory::get_logger("genie::main");
        err_logger->critical("Exception caught. Attempting to exit gracefully: {}", e.what());
        // Explicitly delete chromeTracer here (its destructor is what actually serializes the buffered
        // trace events to disk) so the chrometrace file still gets written on this error path, instead of
        // silently losing every event recorded so far, as happened before this fix.
        if (chromeTracer) {
            delete chromeTracer;
            chromeTracer = nullptr;
        }
#if GLOO_USE_MPI
        // Assumption: There is no other MPI (not just barier, but all MPI call) apart from the one before firing workload.        
        // Calling MPI_Abort is not a good idea because it will kill even the good processes, preventing them from writing their verbs API traces. 
        if (!barrier_with_timeout()) {
            err_logger->critical("Timed out after {}s at the MPI barrier in the error path; other ranks did not finish", GENIE_BARRIER_TIMEOUT_S);
            return 1;
        }
        MPI_Finalize();
#endif
        return 0;
    } catch (...) {
        auto err_logger = AstraSim::LoggerFactory::get_logger("genie::main");
        err_logger->critical("Unknown exception caught");
        // See comment in the catch block above: flush the buffered chrometrace to disk on this error path too.
        if (chromeTracer) {
            delete chromeTracer;
            chromeTracer = nullptr;
        }
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
