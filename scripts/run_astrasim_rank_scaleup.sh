#!/bin/bash
# Wrapper for scaleup runs launched via srun; uses SLURM_PROCID for rank.

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"
PROJECT_DIR="${PROJECT_DIR:-.}"
RANK="${SLURM_PROCID:-${OMPI_COMM_WORLD_RANK:-unknown}}"

exec > "${PROJECT_DIR}/genie_output_${JOBTAG}_${RANK}.log" 2>&1

exec "$@"
