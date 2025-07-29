#ifndef ASTRA_SIM_NETWORK_FRONTEND_GENIE_ARGS_HH
#define ASTRA_SIM_NETWORK_FRONTEND_GENIE_ARGS_HH

struct ParsedArgs {
    std::string workload_config;
    std::string system_config;
    std::string memory_config;
    std::string logical_topology_config;
    int rank;
    std::string rdma_driver;
    int rdma_port;
    std::string redis_ip;
    int redis_num_ranks;
    
    std::string logging_configuration = "empty";
    // Placeholders. The input flags are not included yet.
    std::string comm_group_configuration = "empty";

    // Not directly provided, but parsed from files
    std::vector<int> logical_dims = std::vector<int>();
    std::vector<int> queues_per_dim = std::vector<int>();
    int num_npus = 1;
    int num_queues_per_dim = 1;
};

// Function declarations from args.cc
ParsedArgs parse_arguments(int argc, char* argv[]);
void read_logical_topo_config(ParsedArgs &args);

#endif // ASTRA_SIM_NETWORK_FRONTEND_GENIE_ARGS_HH