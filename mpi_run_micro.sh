#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="${EXAMPLE_DIR:?}/workload"
OUTPUT_PATH="${OUTPUT_PATH:-$(pwd)}"
mkdir -p "${OUTPUT_PATH}"

# paths
WORKLOAD="${WORKLOAD:-${WORKLOAD_DIR}/trace}"
COMM_GROUP="${COMM_GROUP:-${WORKLOAD_DIR:?}/comm_groups.json}"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
NUM_RANKS="${NUM_RANKS:-4}"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_${NUM_RANKS}.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS_PER_NODE="${NUM_RANKS_PER_NODE:-1}"
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

# For vader, remove "-N ${NUM_RANKS_PER_NODE}". Not sure if need to reinstate for tk/champollion
NUMA_NODE=1
# GENIE_TOS sets the GRH traffic_class (TOS byte) for each QP, which determines
# DSCP and PCP on the wire. Default to 128 (= DSCP CS4 = PCP 4) to match the
# port configuration tc/1/traffic_class=128 used by perftest.
# bash ${SCRIPT_DIR}/run_astrasim_rank.sh \
export GENIE_TOS="${GENIE_TOS:-128}"
mpirun \
    ${MCA_STRING} \
    --tag-output \
    -x GENIE_TOS \
    -np ${NUM_RANKS} \
    numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} \
    ${GENIE_BIN} \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --comm_group "${COMM_GROUP}" \
    --ranks_per_node "${NUM_RANKS_PER_NODE}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" \
    --num_iterations "${NUM_ITERATIONS}" \
    > "${OUTPUT_PATH}/stdout_${JOBTAG}.log" 2>&1

