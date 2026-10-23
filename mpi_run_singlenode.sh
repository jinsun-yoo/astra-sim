#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="${EXAMPLE_DIR:?}/workload"
OUTPUT_PATH="${OUTPUT_PATH:-$(pwd)}"
mkdir -p "${OUTPUT_PATH}"
mkdir -p "${OUTPUT_PATH}/stdout"

# paths
WORKLOAD="${WORKLOAD:-${WORKLOAD_DIR}/trace}"
COMM_GROUP="${COMM_GROUP:-${WORKLOAD_DIR:?}/comm_groups.json}"
SYSTEM="${SYSTEM:-${EXAMPLE_DIR:?}/system_2chunk.json}"
REMOTE_MEMORY="${REMOTE_MEMORY:-${EXAMPLE_DIR:?}/remote_memory.json}"
NUM_RANKS="${NUM_RANKS:-4}"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_${NUM_RANKS}.json}"
RDMA_DRIVER="${RDMA_DRIVER:-mlx5_0}"
RDMA_PORT="${RDMA_PORT:-1}"
NUM_RANKS_PER_NODE="${NUM_RANKS_PER_NODE:-${NUM_RANKS}}"
NUM_ITERATIONS="${NUM_ITERATIONS:-1}"

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"
if hostname | grep -q "sith"; then
    MCA_STRING="--mca btl_tcp_if_include bond0"
else
    MCA_STRING=""
fi

if [[ -n "${GENIE_HASH:-}" ]]; then
  GENIE_BIN="${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie_${GENIE_HASH}"
else
  GENIE_BIN="${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie"
fi

# Comma-separated per-local-rank bindings. Default for Vader's current 4-rank
# single-node setup matches rank-to-HCA assignment mlx5_0, mlx5_2, mlx5_8,
# mlx5_10 -> NUMA 0, 0, 1, 1.
NUMA_CPU_NODES="${NUMA_CPU_NODES:-0,0,0,0,1,1,1,1}"
NUMA_MEM_NODES="${NUMA_MEM_NODES:-${NUMA_CPU_NODES}}"

# GENIE_TOS sets the GRH traffic_class (TOS byte) for each QP, which determines
# DSCP and PCP on the wire. Default to 128 (= DSCP CS4 = PCP 4) to match the
# port configuration tc/1/traffic_class=128 used by perftest.
# bash ${SCRIPT_DIR}/run_astrasim_rank.sh \
export GENIE_TOS="${GENIE_TOS:-128}"
export GENIE_BIN WORKLOAD SYSTEM REMOTE_MEMORY LOGICAL_TOPOLOGY COMM_GROUP
export NUM_RANKS_PER_NODE RDMA_DRIVER RDMA_PORT NUM_ITERATIONS
export NUMA_CPU_NODES NUMA_MEM_NODES

MPIRUN_ENV_ARGS=(
    -x GENIE_TOS
    -x GENIE_BIN
    -x WORKLOAD
    -x SYSTEM
    -x REMOTE_MEMORY
    -x LOGICAL_TOPOLOGY
    -x COMM_GROUP
    -x NUM_RANKS_PER_NODE
    -x RDMA_DRIVER
    -x RDMA_PORT
    -x NUM_ITERATIONS
    -x NUMA_CPU_NODES
    -x NUMA_MEM_NODES
)

if [[ -n "${ENABLE_IBVERBS_INTERCEPT:-}" ]]; then
  ROOT_PATH="${ROOT_PATH:-/nfs/jinsun}"
  INTERCEPT_DIR="${ROOT_PATH}/ibverbs_intercept"

  export LD_LIBRARY_PATH="${INTERCEPT_DIR}:${LD_LIBRARY_PATH:-}"
  MPIRUN_ENV_ARGS+=(-x LD_LIBRARY_PATH "-x" "LD_PRELOAD=${INTERCEPT_DIR}/libibverbs_intercept.so")
fi

mpirun \
    ${MCA_STRING} \
    --tag-output \
    "${MPIRUN_ENV_ARGS[@]}" \
    -np ${NUM_RANKS} \
    numactl \
        --cpunodebind=1 \
        --membind=1 \
        ${GENIE_BIN} \
        --workload "${WORKLOAD}" \
        --system "${SYSTEM}" \
        --memory "${REMOTE_MEMORY}" \
        --logical_topology "${LOGICAL_TOPOLOGY}" \
        --comm_group "${COMM_GROUP}" \
        --ranks_per_node "${NUM_RANKS_PER_NODE}" \
        --rdma_driver "${RDMA_DRIVER}" \
        --rdma_port "${RDMA_PORT}" \
        --num_iterations "${NUM_ITERATIONS}" \
    > "${OUTPUT_PATH}/stdout_${JOBTAG}.log" 2>&1
