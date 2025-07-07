SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/gent"

# paths
WORKLOAD="${WORKLOAD:-${EXAMPLE_DIR:?}/workload/AllReduce_1MB}"
SYSTEM="${EXAMPLE_DIR:?}/system.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_4.json}"
NUM_RANKS=4
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1

#bash build/astra_gent/build.sh
#--allow-run-as-root \
echo $RANK
mpirun \
    -n ${NUM_RANKS} \
    ./build/astra_gent/build/bin/AstraSim_Gent  \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" \
