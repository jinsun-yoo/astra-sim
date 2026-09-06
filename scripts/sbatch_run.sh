#!/bin/bash
#SBATCH --job-name=astra-sim-genie
#SBATCH --output=slurm-%j.out
#SBATCH --error=slurm-%j.err
#SBATCH --ntasks=4
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=1
#SBATCH --time=01:00:00
#SBATCH --partition=champollion

set -x

#SCRIPT_DIR=$(dirname "$(realpath "$0")")
SCRIPT_DIR=//nfs/jinsun/astra-sim
PROJECT_DIR="${SCRIPT_DIR:?}"
EXAMPLE_DIR="${PROJECT_DIR:?}/examples/genie"
WORKLOAD_DIR="/nfs/jinsun/chakra_fx/minimal_repro/llama_8b_4rank_tp"
#WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE_MANY_30"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_REDUCE"
# WORKLOAD_DIR="${PROJECT_DIR:?}/ALL_GATHER_4_1024.0"

# paths
#WORKLOAD="${WORKLOAD:-${EXAMPLE_DIR:?}/workload/ALL_GATHER}"
WORKLOAD="${WORKLOAD_DIR}"
SYSTEM="${EXAMPLE_DIR:?}/system_2chunk.json"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory.json"
LOGICAL_TOPOLOGY="${LOGICAL_TOPOLOGY:-${EXAMPLE_DIR:?}/logical_topology_4.json}"
RDMA_DRIVER="mlx5_0"
RDMA_PORT=1
NUM_RANKS=4

module load openmpi

# Use srun instead of mpirun for better SLURM integration
#--mpi=pmix_v3 \
srun \
    --output=%j_output_rank_%t.log \
    --error=%j_error_rank_%t.log \
    --ntasks=${NUM_RANKS} \
    ./build/astra_genie/build/bin/AstraSim_Genie \
    --workload "${WORKLOAD}" \
    --system "${SYSTEM}"  \
    --memory "${REMOTE_MEMORY}"  \
    --logical_topology "${LOGICAL_TOPOLOGY}" \
    --rdma_driver "${RDMA_DRIVER}" \
    --rdma_port "${RDMA_PORT}"

# --logging "${PROJECT_DIR}/logger_config.toml" \