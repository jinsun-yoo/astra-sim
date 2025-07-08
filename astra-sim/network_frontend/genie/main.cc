#include <getopt.h>
#include <unistd.h>

#include "astra-sim/common/Logging.hh"
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


int main(int argc, char* argv[]) {
#ifdef GLOO_USE_MPI
    MPI_Init(NULL, NULL);
#endif

    ParsedArgs args = parse_arguments(argc, argv);

    // Initialize Gloo
    std::cout << "Hello, world!" << std::endl;
    // Device name obtained by running 'rdma dev' on command line
    // Port from 'rdma link'
    auto ibv_attr =
        gloo::transport::ibverbs::attr{args.rdma_driver, args.rdma_port, 0};
    std::cout << "Initialize ibv attr" << std::endl;
    auto dev = gloo::transport::ibverbs::CreateDevice(ibv_attr);
    std::cout << "Initialize ibv dev" << std::endl;

    // Initialize context
#ifdef GLOO_USE_MPI
    auto backingContext = std::make_shared<gloo::mpi::Context>(MPI_COMM_WORLD);
    std::cout << "Created mpi context" << std::endl;
    backingContext->connectFullMesh(dev);
    std::cout << "Connected mesh " << std::endl;
// test_ctx->gloo_context = backingContext;
#endif
    std::cout << "Established Connection!" << std::endl;

#if GLOO_USE_REDIS
    std::shared_ptr<gloo::Context> context;
    int rank;
    int world_size;
    world_size = args.redis_num_ranks;  // Number of participating processes
    rank = args.rank;
    auto redis_context =
        std::make_shared<gloo::rendezvous::Context>(rank, world_size);
    std::cout << "Initialize rendezvous context" << std::endl;
    gloo::rendezvous::RedisStore redis(args.redis_ip);
    std::cout << "Setup Redis Store" << std::endl;
    redis_context->connectFullMesh(redis, dev);
    std::cout << "Complete full mesh" << std::endl;

    sleep(5);  // Sleep for 5 seconds
    if (rank == 0) {
        std::cout << "Rank 0: flushing Redis store" << std::endl;
        redis.flushall();
    }
    context = redis_context;
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
    Analytical::AnalyticalRemoteMemory* mem =
        new Analytical::AnalyticalRemoteMemory(args.memory_config);
    ASTRASimGenieNetwork* network =
        new ASTRASimGenieNetwork(args.rank, backingContext);
    AstraSim::Sys* system = new AstraSim::Sys(
        args.rank, args.workload_config, args.comm_group_configuration,
        args.system_config, mem, network, args.logical_dims, args.queues_per_dim,
        injection_scale, comm_scale, rendezvous_protocol);

    // Synchronization complete. START!!
    // context->getDevice()->releaseDevice();
    network->timekeeper->startTimer();
    system->workload->fire();
    network->threadcounter->WaitThreadsJoin();
    return 0;
}
