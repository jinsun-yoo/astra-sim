#!/bin/bash
set -x

SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
# WORKLOAD_DIR="/nfs/jinsun/chakra_fx/minimal_repro/trace_0729_1500/trace"
WORKLOAD_DIR="/nfs/jinsun/chakra_fx/microbenchmarks/AG_16MB/agcomp_short"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE_MANY_30"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_GATHER_4_1024.0"

# paths
#WORKLOAD="${WORKLOAD:-${EXAMPLE_DIR:?}/workload/ALL_GATHER}"
WORKLOAD="${WORKLOAD_DIR}"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk-16split.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_4.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS=4

JOBTAG=$(date +%m%d_%H%M%S)

# LD_PRELOAD="/nfs/jinsun/ibverbs_intercept/libibverbs_intercept.so" \
mpirun \
    --tag-output \
    -np ${NUM_RANKS} \
    -N 1 \
    taskset --cpu-list 1 \
    bash -c ' \
    perf record -e cpu-clock -F 9999 -g --call-graph dwarf -o perf-$OMPI_COMM_WORLD_RANK.data -- \
    /nfs/jinsun/astra-sim/build/astra_genie/build/bin/AstraSim_Genie --workload /nfs/jinsun/chakra_fx/microbenchmarks/AG_16MB/agcomp --system /nfs/jinsun/astra-sim/examples/genie/system_2chunk-16split.json --memory /nfs/jinsun/astra-sim/examples/genie/remote_memory.json --logical_topology /nfs/jinsun/astra-sim/examples/genie/logical_topology_4.json --rdma_driver mlx5_0 --rdma_port 1
    '

# Alternative profiling methods:

# Method A: Profile with hardware events
# perf record -e cycles,instructions,cache-misses,branch-misses -F 9999 -g --call-graph dwarf

# Method B: Profile specific functions using function entry/exit (requires kernel tracing)
# perf record -e probe:AstraSim_Genie:poll_recv_handler -F 9999 -g

# Method C: Use Intel VTune style profiling
# perf record -e cpu-cycles -e instructions -F 9999 -g --call-graph dwarf

# Method D: Profile memory access patterns
# perf record -e cache-references,cache-misses,page-faults -F 9999 -g

# To analyze the results:
# perf report --stdio | grep poll_recv_handler
# perf script | grep poll_recv_handler
# perf annotate poll_recv_handler

# --logging "${PROJECT_DIR}/logger_config.toml" \
