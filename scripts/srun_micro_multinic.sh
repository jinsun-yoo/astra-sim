#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="${EXAMPLE_DIR:?}/workload/ALL_REDUCE"

# paths
WORKLOAD="${WORKLOAD:-${WORKLOAD_DIR}}"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_4.json}"
RDMA_PORT=1
NUM_RANKS=4

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"

NUMA_NODE=1
# Per-rank wrapper: rank N uses mlx5_N via SLURM_PROCID
RANK_WRAPPER="$(mktemp /home/users/yoojinsu/tmp/astrasim_wrapper_XXXXXX.sh)"
chmod +x "${RANK_WRAPPER}"
cat > "${RANK_WRAPPER}" << EOF
#!/bin/bash
exec numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} \\
    ${SCRIPT_DIR}/build/astra_genie/build/bin/AstraSim_Genie \\
    --workload "${WORKLOAD}" \\
    --system "${SYSTEM}" \\
    --memory "${REMOTE_MEMORY}" \\
    --logical_topology "${LOGICAL_TOPOLOGY}" \\
    --rdma_driver "mlx5_\${SLURM_PROCID}" \\
    --rdma_port "${RDMA_PORT}"
EOF

# LD_PRELOAD="/nfs/jinsun/ibverbs_intercept/libibverbs_intercept.so" \
# IBVERBS_INTERCEPT_EXP_TAG="genie_ibv_trace_${JOBTAG}" \
PROJECT_DIR="${PROJECT_DIR}" \
JOBTAG="${JOBTAG}" \
srun \
    -n ${NUM_RANKS} \
    --ntasks-per-node 4 \
    "${RANK_WRAPPER}" > \
    "${PROJECT_DIR}/output_${JOBTAG}.log" 2>&1
rm -f "${RANK_WRAPPER}"