#include <getopt.h>

#include "astra-sim/system/Sys.hh"
#include "extern/remote_memory_backend/analytical/AnalyticalRemoteMemory.hh"
#include <json/json.hpp>
#include "astra-sim/common/Logging.hh"
#include "gent_network/gent_network.hh"

#include <gloo/rendezvous/context.h>
#include <gloo/rendezvous/redis_store.h>
#include <gloo/transport/ibverbs/pair.h>


using json = nlohmann::json;

// Command line arguments and default values.
std::string comm_group_configuration = "empty";
std::string logging_configuration = "empty";
int num_queues_per_dim = 1;
double comm_scale = 1;
double injection_scale = 1;
bool rendezvous_protocol = false;
auto logical_dims = std::vector<int>();
int num_npus = 1;
auto queues_per_dim = std::vector<int>();

// TODO: Migrate to yaml
void read_logical_topo_config(std::string network_configuration,
                              std::vector<int>& logical_dims) {
    std::ifstream inFile;
    inFile.open(network_configuration);
    if (!inFile) {
        std::cerr << "Unable to open file: " << network_configuration << std::endl;
        exit(1);
    }

    // Find the size of each dimension.
    json j;
    inFile >> j;
    if (j.contains("logical-dims")) {
        std::vector<std::string> logical_dims_str_vec = j["logical-dims"];
        for (auto logical_dims_str : logical_dims_str_vec) {
            logical_dims.push_back(stoi(logical_dims_str));
        }
    }

    // Find the number of all npus.
    std::stringstream dimstr;
    for (auto num_npus_per_dim : logical_dims) {
        num_npus *= num_npus_per_dim;
        dimstr << num_npus_per_dim << ",";
    }
    std::cout << "There are " << num_npus << " npus: " << dimstr.str() << "\n";

    queues_per_dim = std::vector<int>(logical_dims.size(), num_queues_per_dim);
}



struct ParsedArgs {
    std::string workload_config;
    std::string system_config;
    std::string memory_config;
    std::string logical_topology_config;
    int mpi_rank;
    std::string rdma_driver;
    int rdma_port;
    std::string redis_ip;
    int num_ranks;
};

ParsedArgs parse_arguments(int argc, char* argv[]) {
    ParsedArgs args;

    const char* const short_opts = "w:s:m:l:r:r_driver:r_port:r_ip:";
    const option long_opts[] = {
        {"workload", required_argument, nullptr, 'w'},
        {"system", required_argument, nullptr, 's'},
        {"memory", required_argument, nullptr, 'm'},
        {"logical_topology", required_argument, nullptr, 'l'},
        {"mpi_rank", required_argument, nullptr, 'r'},
        {"rdma_driver", required_argument, nullptr, 'd'},
        {"rdma_port", required_argument, nullptr, 'p'},
        {"redis_ip", required_argument, nullptr, 'i'},
        {"num_ranks", required_argument, nullptr, 'n'},
        {nullptr, no_argument, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'w':
                args.workload_config = optarg;
                break;
            case 's':
                args.system_config = optarg;
                break;
            case 'm':
                args.memory_config = optarg;
                break;
            case 'l':
                args.logical_topology_config = optarg;
                break;
            case 'r':
                args.mpi_rank = std::stoi(optarg);
                break;
            case 'd':
                args.rdma_driver = optarg;
                break;
            case 'p':
                args.rdma_port = std::stoi(optarg);
                break;
            case 'i':
                args.redis_ip = optarg;
                break;
            case 'n':
                args.num_ranks = std::stoi(optarg);
                break;
            default:
                std::cerr << "Usage: " << argv[0]
                          << " --workload <workload_config> --system <system_config> --memory <memory_config> "
                          << "--logical_topology <logical_topology_config> --mpi_rank <mpi_rank> "
                          << "--rdma_driver <rdma_driver> --rdma_port <rdma_port> --redis_ip <redis_ip>"
                          << std::endl;
                exit(1);
        }
    }

    if (args.workload_config.empty() || args.system_config.empty() || args.memory_config.empty() || 
        args.logical_topology_config.empty() || args.rdma_driver.empty() || args.redis_ip.empty()) {
        std::cerr << "Error: Missing required arguments." << std::endl;
        exit(1);
    }

    std::cout << "Parsed arguments:" << std::endl;
    std::cout << "  Workload Config: " << args.workload_config << std::endl;
    std::cout << "  System Config: " << args.system_config << std::endl;
    std::cout << "  Memory Config: " << args.memory_config << std::endl;
    std::cout << "  Logical Topology Config: " << args.logical_topology_config << std::endl;
    std::cout << "  MPI Rank: " << args.mpi_rank << std::endl;
    std::cout << "  RDMA Driver: " << args.rdma_driver << std::endl;
    std::cout << "  RDMA Port: " << args.rdma_port << std::endl;
    std::cout << "  Redis IP: " << args.redis_ip << std::endl;

    return args;
}

int main(int argc, char* argv[]){
    ParsedArgs args = parse_arguments(argc, argv);
    std::cout << "Retrieved rank: " << args.mpi_rank << std::endl;

    // Initialize Gloo
    std::cout << "Hello, world!" << std::endl;
    //Device name obtained by running 'rdma dev' on command line
    //Port from 'rdma link'
    auto ibv_attr = gloo::transport::ibverbs::attr{args.rdma_driver, args.rdma_port, 0};
    std::cout << "Initialize ibv attr" << std::endl;
    auto dev = gloo::transport::ibverbs::CreateDevice(ibv_attr);
    std::cout << "Initialize ibv dev" << std::endl;
    // Initialize random seed for random functions within Gloo, that initialize RDMA endpoint addresses.
    std::srand(static_cast<unsigned>(std::hash<std::string>{}(std::to_string(std::time(nullptr)) + std::to_string(args.mpi_rank))));
    std::cout << "Random seed initialized" << std::endl;

    // Initialize context
    auto num_ranks = args.num_ranks;  // Number of participating processes
    auto context = std::make_shared<gloo::rendezvous::Context>(args.mpi_rank, num_ranks);
    std::cout << "Initialize rendezvous context" << std::endl;

    read_logical_topo_config(args.logical_topology_config, logical_dims);
    AstraSim::LoggerFactory::init(logging_configuration);
    Analytical::AnalyticalRemoteMemory* mem =
        new Analytical::AnalyticalRemoteMemory(args.memory_config);
    ASTRASimGentNetwork* network = new ASTRASimGentNetwork(args.mpi_rank, context);
    AstraSim::Sys* system = new AstraSim::Sys(
            args.mpi_rank, args.workload_config, comm_group_configuration,
            args.system_config, mem, network, logical_dims,
            queues_per_dim, injection_scale, comm_scale, rendezvous_protocol);

    gloo::rendezvous::RedisStore redis(args.redis_ip);
    std::cout << "Setup Redis Store" <<std::endl;
    context->connectFullMesh(redis, dev);
    std::cout << "Complete full mesh" << std::endl;

    // Synchronization complete. START!!
    network->timekeeper.startTime();
    system->workload->fire();
    network->threadpooler->WaitThreadsJoin();
    return 0;
}
