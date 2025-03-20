SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/gent"

# paths
#WORKLOAD="${EXAMPLE_DIR:?}/workload/one_comp_node"
WORKLOAD="${EXAMPLE_DIR:?}/workload/AllReduce_1MB"
SYSTEM="${EXAMPLE_DIR:?}/system.json"
NETWORK="${EXAMPLE_DIR:?}/network.yml"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${EXAMPLE_DIR:?}/logical_topology.json"
NUM_RANKS=16

#bash build/astra_gent/build.sh
echo $MPI_RANK
./build/astra_gent/build/bin/AstraSim_Gent  \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --mpi_rank "${MPI_RANK}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" \
    --redis_ip "${REDIS_IP}" \
    --num_ranks "${NUM_RANKS}" \
