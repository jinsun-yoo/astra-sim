#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="/nfs/jinsun/chakra_fx/minimal_repro/llama_8b_4rank_tp"
#WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE_MANY_30"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_GATHER_4_1024.0"

# paths
#WORKLOAD="${WORKLOAD:-${EXAMPLE_DIR:?}/workload/ALL_GATHER}"
WORKLOAD="${WORKLOAD_DIR}"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_4.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS=4

./build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware \
--workload-configuration "${WORKLOAD}" \
--system-configuration "${SYSTEM}"  \
--remote-memory-configuration "${REMOTE_MEMORY}"  \
--network-configuration "${EXAMPLE_DIR:?}/analytical_network.yml" \

# --logging "${PROJECT_DIR}/logger_config.toml" \