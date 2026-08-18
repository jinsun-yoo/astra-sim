#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <getopt.h>
#include <json/json.hpp>

#include "args.hh"
#include "astra-sim/common/Logging.hh"

using json = nlohmann::json;


void read_logical_topo_config(ParsedArgs &args) {
    auto logger = AstraSim::LoggerFactory::get_logger("genie::args");
    std::ifstream inFile;
    inFile.open(args.logical_topology_config);
    if (!inFile) {
        throw std::runtime_error("Unable to open file: " + args.logical_topology_config);
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

    args.queues_per_dim = std::vector<int>(args.logical_dims.size(), args.num_queues_per_dim);
}



ParsedArgs parse_arguments(int argc, char* argv[]) {
    ParsedArgs args;


    const char* const short_opts = "w:s:m:l:g:d:p:x:r:R:i:n:c:q:t:";
    const option long_opts[] = {
        {"workload", required_argument, nullptr, 'w'},
        {"system", required_argument, nullptr, 's'},
        {"memory", required_argument, nullptr, 'm'},
        {"logical_topology", required_argument, nullptr, 'l'},
        {"logging", required_argument, nullptr, 'g'},
        {"rdma_driver", required_argument, nullptr, 'd'},
        {"rdma_port", required_argument, nullptr, 'p'},
        {"rdma_gid_index", required_argument, nullptr, 'x'},
        {"ranks_per_node", required_argument, nullptr, 'r'},
        // Only needed when using redis backend for rdzv.
        {"redis_rank", required_argument, nullptr, 'R'},
        {"redis_ip", required_argument, nullptr, 'i'},
        {"redis_num_ranks", required_argument, nullptr, 'n'},
        {"comm_group", required_argument, nullptr, 'c'},
        {"num_qps", required_argument, nullptr, 'q'},
        {"num_iterations", required_argument, nullptr, 't'},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
           -1) {
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
            args.ranks_per_node = std::stoi(optarg);
            break;
        case 'R':
            args.rank = std::stoi(optarg);
            break;
        case 'd':
            args.rdma_driver = optarg;
            break;
        case 'p':
            args.rdma_port = std::stoi(optarg);
            break;
        case 'x':
            args.rdma_gid_index = std::stoi(optarg);
            break;
        case 'i':
            args.redis_ip = optarg;
            break;
        case 'n':
            args.redis_num_ranks = std::stoi(optarg);
            break;
        case 'c':
            args.comm_group_configuration = optarg;
            break;
        case 'q':
            args.num_qps = std::stoi(optarg);
            break;
        case 't':
            args.num_iterations = std::stoi(optarg);
            break;
        default:
            throw std::runtime_error(
                "Cannot recognize flag " + std::to_string(opt) +
                " with arg: " + (optarg ? std::string(optarg) : "") +
                " Usage: " + std::string(argv[0]) +
                " --workload <workload_config> --system <system_config>"
                " --memory <memory_config> --logical_topology <logical_topology_config>"
                " --redis_rank <redis_rank> --redis_num_ranks <redis_num_ranks>"
                " --rdma_driver <rdma_driver> --rdma_port <rdma_port>"
                " --redis_ip <redis_ip> --redis_num_ranks <num_ranks>");
        }
    }

    if (args.workload_config.empty() || args.system_config.empty() ||
        args.memory_config.empty() || args.logical_topology_config.empty() ||
        args.rdma_driver.empty()) {
        throw std::runtime_error(
            "Missing one of required arguments (workload/system/memory/logical_topology config OR rdma driver).");
    }

    if (args.num_iterations < 1) {
        throw std::runtime_error(
            "num_iterations must be >= 1, got: " + std::to_string(args.num_iterations));
    }

    // If a comm_group file was specified but doesn't exist, treat it as "empty"
    // so that initialize_comm_group skips it gracefully.
    if (args.comm_group_configuration.find("empty") == std::string::npos) {
        std::ifstream f(args.comm_group_configuration);
        if (!f.good()) {
            AstraSim::LoggerFactory::get_logger("genie::args")
                ->warn("comm_group file: {} not found. Treating as empty.",
                       args.comm_group_configuration);
            args.comm_group_configuration = "empty";
        }
    }

    return args;
}