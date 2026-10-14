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
MPIRUN_ENV_ARGS=()
if [[ -n "${ENABLE_IBVERBS_INTERCEPT:-}" ]]; then
  ROOT_PATH="${ROOT_PATH:-/nfs/jinsun}"
  INTERCEPT_DIR="${ROOT_PATH}/ibverbs_intercept"

  export LD_LIBRARY_PATH="${INTERCEPT_DIR}:${LD_LIBRARY_PATH:-}"
  export LD_PRELOAD="${INTERCEPT_DIR}/libibverbs_intercept.so"
  MPIRUN_ENV_ARGS=(-x LD_LIBRARY_PATH -x LD_PRELOAD)
fi

# Ranks are mapped ppr:NUM_RANKS_PER_NODE per node. Each local rank is bound by
# numactl to the NUMA node GENIE_NUMA_NODES[local_rank] (comma-separated list),
# falling back to NUMA_NODE when the list is unset. mpirun's own binding is
# disabled so it cannot conflict with numactl.
MPIRUN_PLACEMENT_ARGS=()
RANK_COMMAND=(numactl --cpunodebind="${NUMA_NODE}" --membind="${NUMA_NODE}")
if [[ ! "${NUM_RANKS_PER_NODE}" =~ ^[1-9][0-9]*$ ]] ||
   [[ ! "${NUM_RANKS}" =~ ^[1-9][0-9]*$ ]] ||
   (( NUM_RANKS % NUM_RANKS_PER_NODE != 0 )); then
  echo "Placement requires positive ranks and whole hosts of NUM_RANKS_PER_NODE ranks" >&2
  exit 1
fi
# Per-core CPU pinning (disabled; replaced by per-rank NUMA binding):
# export GENIE_CPU_CORES="${GENIE_CPU_CORES:-}"
# MPIRUN_ENV_ARGS+=(-x GENIE_CPU_CORES)
export GENIE_NUMA_NODES="${GENIE_NUMA_NODES:-}"
MPIRUN_ENV_ARGS+=(-x GENIE_NUMA_NODES)
MPIRUN_PLACEMENT_ARGS=(--map-by "ppr:${NUM_RANKS_PER_NODE}:node" --bind-to none)
RANK_COMMAND=(bash -c '
  set -euo pipefail
  : "${OMPI_COMM_WORLD_LOCAL_RANK:?Open MPI local rank is required}"
  if [[ ! "${OMPI_COMM_WORLD_LOCAL_RANK}" =~ ^[0-9]+$ ]]; then
    echo "Invalid Open MPI local rank: ${OMPI_COMM_WORLD_LOCAL_RANK}" >&2
    exit 1
  fi
  local_rank=${OMPI_COMM_WORLD_LOCAL_RANK}
  numa_node=$1
  shift
  if [[ -n "${GENIE_NUMA_NODES}" ]]; then
    IFS=, read -r -a numas <<< "${GENIE_NUMA_NODES}"
    if (( local_rank >= ${#numas[@]} )) || [[ ! "${numas[local_rank]}" =~ ^[0-9]+$ ]]; then
      echo "No valid Genie NUMA node mapping for local rank ${local_rank}" >&2
      exit 1
    fi
    numa_node=${numas[local_rank]}
  fi
  echo "Genie local rank ${local_rank}: NUMA node ${numa_node}" >&2
  # if [[ -n "${GENIE_CPU_CORES}" ]]; then
  #   IFS=, read -r -a cpus <<< "${GENIE_CPU_CORES}"
  #   if (( local_rank >= ${#cpus[@]} )) || [[ ! "${cpus[local_rank]}" =~ ^[0-9]+$ ]]; then
  #     echo "No valid Genie CPU mapping for local rank ${local_rank}" >&2
  #     exit 1
  #   fi
  #   echo "Genie local rank ${local_rank}: Linux CPU ${cpus[local_rank]}" >&2
  #   exec taskset --cpu-list "${cpus[local_rank]}" "$@"
  # fi
  exec numactl --cpunodebind="${numa_node}" --membind="${numa_node}" "$@"
' bash "${NUMA_NODE}")

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

