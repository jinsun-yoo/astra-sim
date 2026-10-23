#!/bin/bash
# Like mpi_run_micro.sh but uses srun so it inherits the full SLURM allocation.
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
NUM_RANKS="${NUM_RANKS:-32}"
NUM_RANKS_PER_NODE="${NUM_RANKS_PER_NODE:-8}"
NUM_NODES="${NUM_NODES:-$((NUM_RANKS / NUM_RANKS_PER_NODE))}"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_${NUM_RANKS}.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"

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
srun \
    --mpi=pmi2 \
    -n ${NUM_RANKS} \
    -N ${NUM_NODES} \
    --ntasks-per-node=${NUM_RANKS_PER_NODE} \
    bash ${SCRIPT_DIR}/run_astrasim_rank_scaleup.sh \
    numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} \
    ${GENIE_BIN} \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --comm_group "${COMM_GROUP}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}"
