#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"

# paths
WORKLOAD="${EXAMPLE_DIR}/workload/ALL_REDUCE"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk-16split.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${EXAMPLE_DIR:?}/logical_topology_4.json"
NUM_RANKS=4
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1

JOBTAG=$(date +%m%d_%H%M%S);

# IBVERBS_INTERCEPT_EXP_TAG="genie_ibv_trace_${JOBTAG}" \
# LD_PRELOAD="/nfs/jinsun/ibverbs_intercept/libibverbs_intercept.so" \
mpirun \
    --tag-output \
    -np ${NUM_RANKS} \
    -N 1 \
    # To pin to one core. Needed to ensure sequential timeline in tracing. Remove if needed.
    taskset --cpu-list 8 \ 
    ${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" > \
    "${PROJECT_DIR}/output_${JOBTAG}.log" 2>&1
