#!/bin/bash
#set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
# WORKLOAD_DIR="/nfs/jinsun/chakra_fx/minimal_repro/llama_tp4"
WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE"

# paths
#WORKLOAD="${WORKLOAD:-${EXAMPLE_DIR:?}/workload/ALL_GATHER}"
WORKLOAD="${WORKLOAD_DIR}"
SYSTEM="${EXAMPLE_DIR:?}/system.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_4.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS=4

GDB_DEBUG=True LD_PRELOAD=/nfs/jinsun/ibverbs_intercept/libibverbs_intercept_debug.so mpirun \
    -np 1 \
    -N 1 \
    gdb --args \
    ./build/astra_genie/build/bin/AstraSim_Genie \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" \
    : \
    -np $((NUM_RANKS-1)) \
    -N 1 \
    ./build/astra_genie/build/bin/AstraSim_Genie \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" \
