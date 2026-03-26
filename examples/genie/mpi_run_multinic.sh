#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}/../../"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"

# Paths
WORKLOAD="${EXAMPLE_DIR}/workload/ALL_REDUCE"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${EXAMPLE_DIR:?}/logical_topology_4.json"

NUM_RANKS=4
RDMA_DRIVERS=("mlx5_0" "mlx5_1" "mlx5_2" "mlx5_3")
RDMA_PORT=1
RDMA_GID_INDEX=${RDMA_GID_INDEX:-3}

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"
export CHROMETRACE_FILENAME_DATETIME=${JOBTAG}

declare -A NODE_TO_CPUS
declare -A NODE_CPU_INDEX

get_nic_numa_node() {
    local nic="$1"
    local node
    node=$(cat /sys/class/infiniband/${nic}/device/numa_node 2>/dev/null || echo 0)
    if [ -z "$node" ] || [ "$node" -lt 0 ]; then
        echo "Warning: NIC ${nic} has no NUMA node, defaulting to 0"
        node=0
    fi
    echo "$node"
}

init_node_cpus() {
    local node="$1"
    local cpus

    if [ -n "${NODE_TO_CPUS[$node]:-}" ]; then
        return
    fi

    cpus=$(lscpu -p=CPU,NODE | grep -v "^#" | awk -F, -v node="$node" '$2==node {printf "%s ", $1}' | sed 's/[[:space:]]*$//')

    if [ -z "$cpus" ]; then
        echo "Warning: NUMA node ${node} has no CPUs, defaulting to CPU 0"
        cpus="0"
    fi

    NODE_TO_CPUS["$node"]="$cpus"
    NODE_CPU_INDEX["$node"]=0
}

get_next_cpu_for_nic() {
    local nic="$1"
    local node
    local cpus_str
    local idx
    local cpu_count
    local -a cpus

    node=$(get_nic_numa_node "$nic")
    init_node_cpus "$node"

    cpus_str="${NODE_TO_CPUS[$node]}"
    read -r -a cpus <<< "$cpus_str"
    idx=${NODE_CPU_INDEX[$node]}
    cpu_count=${#cpus[@]}

    REPLY="${cpus[$((idx % cpu_count))]}"
    NODE_CPU_INDEX["$node"]=$(((idx + 1) % cpu_count))
}

# Build mpirun MPMD command
MPIRUN_CMD="mpirun --tag-output"

for ((i=0; i<NUM_RANKS; i++)); do
    NIC=${RDMA_DRIVERS[i]}
    get_next_cpu_for_nic "$NIC"
    CPU="$REPLY"
    echo "Rank $i -> NIC $NIC, CPU $CPU"

    MPIRUN_CMD+=" -np 1 taskset --cpu-list ${CPU} ${PROJECT_DIR}/build/astra_genie/build/bin/AstraSim_Genie \
        --workload \"${WORKLOAD}\" \
        --system \"${SYSTEM}\" \
        --memory \"${REMOTE_MEMORY}\" \
        --logical_topology \"${LOGICAL_TOPOLOGY}\" \
        --rdma_driver \"${NIC}\" \
        --rdma_port ${RDMA_PORT} \
        --rdma_gid_index ${RDMA_GID_INDEX} :"
done

# Remove trailing colon
MPIRUN_CMD=${MPIRUN_CMD%:}

declare -A IB_BEFORE

capture_ib_counters() {
    set +x
    for nic in "${RDMA_DRIVERS[@]}"; do
        for f in /sys/class/infiniband/${nic}/ports/1/counters/* \
                 /sys/class/infiniband/${nic}/ports/1/hw_counters/*; do
            IB_BEFORE["${nic}/$(basename $f)"]=$(cat "$f" 2>/dev/null || echo 0)
        done
    done
    set -x
}

print_ib_delta() {
    set +x
    echo "=== IB Counters: DELTA ==="
    for nic in "${RDMA_DRIVERS[@]}"; do
        local printed_nic=0
        for f in /sys/class/infiniband/${nic}/ports/1/counters/* \
                 /sys/class/infiniband/${nic}/ports/1/hw_counters/*; do
            key="${nic}/$(basename $f)"
            after=$(cat "$f" 2>/dev/null || echo 0)
            before=${IB_BEFORE["$key"]:-0}
            delta=$(( after - before ))
            if [ "$delta" != "0" ]; then
                [ "$printed_nic" = "0" ] && echo "--- ${nic} ---" && printed_nic=1
                echo "  $(basename $f): +${delta}"
            fi
        done
    done
    echo "=== End IB Counters: DELTA ==="
    set -x
}

# Run
capture_ib_counters
eval $MPIRUN_CMD > "${PROJECT_DIR}/output_${JOBTAG}.log" 2>&1
print_ib_delta >> "${PROJECT_DIR}/output_${JOBTAG}.log" 2>&1
