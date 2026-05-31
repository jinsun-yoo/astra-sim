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
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_${NUM_RANKS}.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS="${NUM_RANKS:-4}"
NUM_RANKS_PER_NODE="${NUM_RANKS_PER_NODE:-1}"

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

NUMA_NODE=1
LD_PRELOAD="${ROOT_PATH}/ibverbs_intercept/libibverbs_intercept.so" \
IBVERBS_INTERCEPT_EXP_TAG="genie_ibv_trace_${JOBTAG}" \
PROJECT_DIR="${PROJECT_DIR}" \
JOBTAG="${JOBTAG}" \
mpirun \
    ${MCA_STRING} \
    --tag-output \
    -np ${NUM_RANKS} \
    -N ${NUM_RANKS_PER_NODE} \
    bash ${SCRIPT_DIR}/run_astrasim_rank.sh \
    numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} \
    ${GENIE_BIN} \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --comm_group "${COMM_GROUP}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" 

