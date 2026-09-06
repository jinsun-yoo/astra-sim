#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="${EXAMPLE_DIR:?}/workload"

RUN_MODE="${RUN_MODE:-workload}"
ROOT_PATH="${ROOT_PATH:?ROOT_PATH must be set}"
OUTPUT_PATH="${OUTPUT_PATH:?OUTPUT_PATH must be set}"
CLUSTER_NAME="${CLUSTER_NAME:-vader}"
JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"

NUM_RANKS="${NUM_RANKS:-4}"
NUM_RANKS_PER_NODE="${NUM_RANKS_PER_NODE:-1}"
NUMA_NODE="${NUMA_NODE:-1}"

SYSTEM="${SYSTEM:-${EXAMPLE_DIR:?}/system_2chunk.json}"
REMOTE_MEMORY="${REMOTE_MEMORY:-${EXAMPLE_DIR:?}/remote_memory.json}"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_${NUM_RANKS}.json}"
COMM_GROUP="${COMM_GROUP:-${WORKLOAD_DIR:?}/comm_groups.json}"
RDMA_DRIVER="${RDMA_DRIVER:-mlx5_0}"
RDMA_PORT="${RDMA_PORT:-1}"

COLLECTIVE="${COLLECTIVE:-allreduce}"
MESSAGE_SIZE_MB="${MESSAGE_SIZE_MB:-8}"
WORKLOAD_ITER_DIR="${WORKLOAD_ITER_DIR:-30iter}"
NPUS_DIR="${NPUS_DIR:-${NUM_RANKS}npus}"

RUN_OUTPUT_DIR="${OUTPUT_PATH}/run_${JOBTAG}"
mkdir -p "${RUN_OUTPUT_DIR}"

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

if [[ ! -x "${GENIE_BIN}" ]]; then
    echo "Genie binary not found or not executable: ${GENIE_BIN}" >&2
    exit 2
fi

case "${RUN_MODE}" in
    workload)
        WORKLOAD="${WORKLOAD:?WORKLOAD must be set for RUN_MODE=workload}"
        ;;
    collective)
        case "${COLLECTIVE,,}" in
            allgather)
                COLLECTIVE_PREFIX="ALL_GATHER"
                ;;
            alltoall)
                COLLECTIVE_PREFIX="ALLTOALL"
                ;;
            allreduce)
                COLLECTIVE_PREFIX="ALL_REDUCE"
                ;;
            reducescatter)
                COLLECTIVE_PREFIX="REDUCE_SCATTER"
                ;;
            sendrecv)
                COLLECTIVE_PREFIX="SENDRECV_UNIDIRECTIONAL"
                ;;
            *)
                echo "Unsupported COLLECTIVE='${COLLECTIVE}'. Use allgather|alltoall|allreduce|reducescatter|sendrecv." >&2
                exit 2
                ;;
        esac
        WORKLOAD="${WORKLOAD_DIR}/microbenchmark/${WORKLOAD_ITER_DIR}/${NPUS_DIR}/${COLLECTIVE_PREFIX}_${MESSAGE_SIZE_MB}"
        ;;
    *)
        echo "Unsupported RUN_MODE='${RUN_MODE}'. Use workload or collective." >&2
        exit 2
        ;;
esac

if [[ ! -f "${WORKLOAD}.0.et" ]]; then
    echo "Workload rank file missing: ${WORKLOAD}.0.et" >&2
    exit 2
fi

if [[ ! -f "${COMM_GROUP}" ]]; then
    echo "Comm group file missing: ${COMM_GROUP}" >&2
    exit 2
fi

IBVERBS_TAG="genie_ibv_trace_${JOBTAG}"
export LD_PRELOAD="${ROOT_PATH}/ibverbs_intercept/libibverbs_intercept.so"
export IBVERBS_INTERCEPT_EXP_TAG="${IBVERBS_TAG}"
export PROJECT_DIR="${RUN_OUTPUT_DIR}"
export JOBTAG

pushd "${SCRIPT_DIR}" >/dev/null
mpirun \
    ${MCA_STRING} \
    --tag-output \
    -np "${NUM_RANKS}" \
    -N "${NUM_RANKS_PER_NODE}" \
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
popd >/dev/null

shopt -s nullglob
json_candidates=("${SCRIPT_DIR}/${IBVERBS_TAG}"*.json "${RUN_OUTPUT_DIR}/${IBVERBS_TAG}"*.json)
if [[ ${#json_candidates[@]} -gt 0 ]]; then
    for file in "${json_candidates[@]}"; do
        if [[ -f "${file}" ]]; then
            mv "${file}" "${RUN_OUTPUT_DIR}/"
        fi
    done
else
    echo "Warning: no interceptor JSON files found for tag ${IBVERBS_TAG}" >&2
fi
shopt -u nullglob

echo "Run complete: ${RUN_OUTPUT_DIR}"
