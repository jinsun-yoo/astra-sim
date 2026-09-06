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
NUM_RANKS="${NUM_RANKS:-4}"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_${NUM_RANKS}.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS_PER_NODE="${NUM_RANKS_PER_NODE:-1}"

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"

if [[ -n "${GENIE_HASH:-}" ]]; then
  GENIE_BIN="${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie_${GENIE_HASH}"
else
  GENIE_BIN="${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie"
fi

NUMA_NODE=1
# GENIE_TOS sets the GRH traffic_class (TOS byte) for each QP, which determines
# DSCP and PCP on the wire. Default to 128 (= DSCP CS4 = PCP 4) to match the
# port configuration tc/1/traffic_class=128 used by perftest.
export GENIE_TOS="${GENIE_TOS:-128}"

srun \
  --mpi=pmix_v4 \
    --label \
    --ntasks "${NUM_RANKS}" \
    --ntasks-per-node "${NUM_RANKS_PER_NODE}" \
    --export=ALL,GENIE_TOS \
    bash "${SCRIPT_DIR}/run_astrasim_rank.sh" \
    numactl --cpunodebind="${NUMA_NODE}" --membind="${NUMA_NODE}" \
    "${GENIE_BIN}" \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}" \
    --memory "${REMOTE_MEMORY}" \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --comm_group "${COMM_GROUP}" \
    --ranks_per_node "${NUM_RANKS_PER_NODE}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}"
