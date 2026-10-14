#!/bin/bash
# Variant of mpi_run_micro.sh: pin each local rank to one Linux CPU core with
# taskset (GENIE_CPU_CORES, comma-separated, indexed by Open MPI local rank).
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

export GENIE_TOS="${GENIE_TOS:-128}"
MPIRUN_ENV_ARGS=()
if [[ -n "${ENABLE_IBVERBS_INTERCEPT:-}" ]]; then
  ROOT_PATH="${ROOT_PATH:-/nfs/jinsun}"
  INTERCEPT_DIR="${ROOT_PATH}/ibverbs_intercept"

  export LD_LIBRARY_PATH="${INTERCEPT_DIR}:${LD_LIBRARY_PATH:-}"
  export LD_PRELOAD="${INTERCEPT_DIR}/libibverbs_intercept.so"
  MPIRUN_ENV_ARGS=(-x LD_LIBRARY_PATH -x LD_PRELOAD)
fi

if [[ ! "${NUM_RANKS_PER_NODE}" =~ ^[1-9][0-9]*$ ]] ||
   [[ ! "${NUM_RANKS}" =~ ^[1-9][0-9]*$ ]] ||
   (( NUM_RANKS % NUM_RANKS_PER_NODE != 0 )); then
  echo "Placement requires positive ranks and whole hosts of NUM_RANKS_PER_NODE ranks" >&2
  exit 1
fi
: "${GENIE_CPU_CORES:?GENIE_CPU_CORES is required}"
export GENIE_CPU_CORES
MPIRUN_ENV_ARGS+=(-x GENIE_CPU_CORES)
MPIRUN_PLACEMENT_ARGS=(--map-by "ppr:${NUM_RANKS_PER_NODE}:node" --bind-to none)
RANK_COMMAND=(bash -c '
  set -euo pipefail
  : "${OMPI_COMM_WORLD_LOCAL_RANK:?Open MPI local rank is required}"
  if [[ ! "${OMPI_COMM_WORLD_LOCAL_RANK}" =~ ^[0-9]+$ ]]; then
    echo "Invalid Open MPI local rank: ${OMPI_COMM_WORLD_LOCAL_RANK}" >&2
    exit 1
  fi
  local_rank=${OMPI_COMM_WORLD_LOCAL_RANK}
  IFS=, read -r -a cpus <<< "${GENIE_CPU_CORES}"
  if (( local_rank >= ${#cpus[@]} )) || [[ ! "${cpus[local_rank]}" =~ ^[0-9]+$ ]]; then
    echo "No valid Genie CPU mapping for local rank ${local_rank}" >&2
    exit 1
  fi
  echo "Genie local rank ${local_rank}: Linux CPU ${cpus[local_rank]}" >&2
  exec taskset --cpu-list "${cpus[local_rank]}" "$@"
' bash)

mpirun \
    ${MCA_STRING} \
    "${MPIRUN_PLACEMENT_ARGS[@]}" \
    --tag-output \
    -x GENIE_TOS \
    "${MPIRUN_ENV_ARGS[@]}" \
    -np ${NUM_RANKS} \
    "${RANK_COMMAND[@]}" \
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
    > "${OUTPUT_PATH}/stdout/stdout_${JOBTAG}.log" 2>&1
