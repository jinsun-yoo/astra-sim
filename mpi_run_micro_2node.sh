#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="${EXAMPLE_DIR:?}/workload"

# paths
WORKLOAD="${WORKLOAD:-${WORKLOAD_DIR}/trace}"
COMM_GROUP="${COMM_GROUP:-${WORKLOAD_DIR:?}/comm_groups.json}"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_4.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS=4

if [[ -n "${GENIE_HASH:-}" ]]; then
  GENIE_BIN="${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie_${GENIE_HASH}"
else
  GENIE_BIN="${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie"
fi

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"
ulimit -Hl   # hard limit
ulimit -Sl   # soft limit
NUMA_NODE=1
LD_PRELOAD="/nfs/jinsun/ibverbs_intercept/libibverbs_intercept.so" \
IBVERBS_INTERCEPT_EXP_TAG="genie_ibv_trace_${JOBTAG}" \
PROJECT_DIR="${PROJECT_DIR}" \
JOBTAG="${JOBTAG}" \
srun \
    -n ${NUM_RANKS} \
    -N 2 \
    --ntasks-per-node=$((NUM_RANKS / 2)) \
    bash ${SCRIPT_DIR}/run_astrasim_rank_2node.sh \
    numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} \
    ${GENIE_BIN} \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --comm_group "${COMM_GROUP}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" 
