#!/bin/bash
# Wrapper script to redirect each rank's output to a separate file
# OMPI_COMM_WORLD_RANK is set by mpirun for each process

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"
PROJECT_DIR="${PROJECT_DIR:-.}"
RANK="${OMPI_COMM_WORLD_RANK:-${SLURM_PROCID:-unknown}}"

case "${RANK}" in
    0) NUMA_NODE=0; RDMA_DRIVER=mlx5_0 ;;
    1) NUMA_NODE=1; RDMA_DRIVER=mlx5_11 ;;
    2) NUMA_NODE=0; RDMA_DRIVER=mlx5_0 ;;
    3) NUMA_NODE=1; RDMA_DRIVER=mlx5_11 ;;
    *) echo "Rank ${RANK} output will be saved to ${PROJECT_DIR}/output_${JOBTAG}_${RANK}.log" ;;
esac

exec > "${PROJECT_DIR}/output_${JOBTAG}_${RANK}.log" 2>&1

# Now execute the actual AstraSim binary with all arguments passed through
exec "$@"
