#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="/nfs/jinsun/chakra_fx/minimal_repro/trace_0729_1500/trace"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE_MANY_30"
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

JOBTAG=$(date +%m%d_%H%M%S)

LD_PRELOAD=/nfs/jinsun/ibverbs_intercept/libibverbs_intercept.so mpirun \
    --tag-output \
    -np ${NUM_RANKS} \
    -N 1 \
    ./build/astra_genie/build/bin/AstraSim_Genie \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" > \
    "${PROJECT_DIR}/output_${JOBTAG}.log" 2>&1

# --logging "${PROJECT_DIR}/logger_config.toml" \