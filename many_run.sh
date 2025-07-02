SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/gent"

# paths
#WORKLOAD="${EXAMPLE_DIR:?}/workload/microbenchmark/ALL_REDUCE_30"
WORKLOAD="${EXAMPLE_DIR:?}/../../ALL_REDUCE_MANY_30"
SYSTEM="${EXAMPLE_DIR:?}/system_simple.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${EXAMPLE_DIR:?}/logical_topology_4.json"
NUM_RANKS=4
REDIS_IP=10.93.232.61

#bash build/astra_gent/build.sh
echo $RANK
echo ./build/astra_gent/build/bin/AstraSim_Gent  \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" \
    --rank "${RANK}" \
    --redis_ip "${REDIS_IP}" \
    --num_ranks "${NUM_RANKS}"

./build/astra_gent/build/bin/AstraSim_Gent  \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}" \
    --rank "${RANK}" \
    --redis_ip "${REDIS_IP}" \
    --num_ranks "${NUM_RANKS}"
