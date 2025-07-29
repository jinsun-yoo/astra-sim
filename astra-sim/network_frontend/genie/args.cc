#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <getopt.h>
#include <json/json.hpp>

#include "args.hh"

using json = nlohmann::json;


void read_logical_topo_config(ParsedArgs &args) {
    std::ifstream inFile;
    inFile.open(args.logical_topology_config);
    if (!inFile) {
        std::cerr << "Unable to open file: " << args.logical_topology_config << std::endl;
        std::cout << "Error1" << std::endl;  
        exit(1);
    }

    // Find the size of each dimension.
    json j;
    inFile >> j;
    if (j.contains("logical-dims")) {
        std::vector<std::string> logical_dims_str_vec = j["logical-dims"];
        for (auto logical_dims_str : logical_dims_str_vec) {
            args.logical_dims.push_back(stoi(logical_dims_str));
        }
    }

    // Find the number of all npus.
    std::stringstream dimstr;
    for (auto num_npus_per_dim : args.logical_dims) {
        args.num_npus *= num_npus_per_dim;
        dimstr << num_npus_per_dim << ",";
    }
    std::cout << "There are " << args.num_npus << " npus: " << dimstr.str() << "\n";

    args.queues_per_dim = std::vector<int>(args.logical_dims.size(), args.num_queues_per_dim);
}



ParsedArgs parse_arguments(int argc, char* argv[]) {
    ParsedArgs args;

    const char* const short_opts = "w:s:m:l:g:r:d:p:i:n";
    const option long_opts[] = {
        {"workload", required_argument, nullptr, 'w'},
        {"system", required_argument, nullptr, 's'},
        {"memory", required_argument, nullptr, 'm'},
        {"logical_topology", required_argument, nullptr, 'l'},
        {"logging", required_argument, nullptr, 'g'},
        {"rank", required_argument, nullptr, 'r'},
        {"rdma_driver", required_argument, nullptr, 'd'},
        {"rdma_port", required_argument, nullptr, 'p'},
        // Only needed when using redis backend for rdzv.
        {"redis_ip", required_argument, nullptr, 'i'},
        {"redis_num_ranks", required_argument, nullptr, 'n'},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
           -1) {
        std::cout << "Parsing argument: " << opt << " with arg: " << optarg
                  << std::endl;
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
        case 'g':
            args.logging_configuration = optarg;
            break;
        case 'r':
            args.rank = std::stoi(optarg);
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
            args.redis_num_ranks = std::stoi(optarg);
            break;
        default:
            std::cerr
                << "Cannot recognize flag " << opt << " with arg: " << optarg
                << "Usage: " << argv[0]
                << " --workload <workload_config> --system <system_config> "
                << "--memory <memory_config> --logical_topology <logical_topology_config>"
                << "--rank <rank> --redis_num_ranks <redis_num_ranks>"
                << "--rdma_driver <rdma_driver> --rdma_port <rdma_port> "
                << "--redis_ip <redis_ip> --redis_num_ranks <num_ranks>"
                << std::endl;
            std::cerr 
                << "Arguments starting with 'redis' are only needed for redis rdzv backend."
                << std::endl;
        std::cout << "Error1" << std::endl;  
            exit(1);
        }
    }

    if (args.workload_config.empty() || args.system_config.empty() ||
        args.memory_config.empty() || args.logical_topology_config.empty() ||
        args.rdma_driver.empty()) {
        std::cerr << "Error: Missing one of required arguments (workload/system/memory/logical_topology config OR rdma driver)." << std::endl;
        std::cout << "Error1" << std::endl;  
        exit(1);
    }

    std::cout << "Parsed arguments:" << std::endl;
    std::cout << "  Workload Config: " << args.workload_config << std::endl;
    std::cout << "  System Config: " << args.system_config << std::endl;
    std::cout << "  Memory Config: " << args.memory_config << std::endl;
    std::cout << "  Logical Topology Config: " << args.logical_topology_config << std::endl;
    std::cout << "  MPI Rank: " << args.rank << std::endl;
    std::cout << "  RDMA Driver: " << args.rdma_driver << std::endl;
    std::cout << "  RDMA Port: " << args.rdma_port << std::endl;
    std::cout << "  Redis IP: " << args.redis_ip << std::endl;

    return args;
}