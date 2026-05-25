#!/bin/bash
# Wrapper script to redirect each rank's output to a separate file
# OMPI_COMM_WORLD_RANK is set by mpirun for each process

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"
PROJECT_DIR="${PROJECT_DIR:-.}"
RANK="${OMPI_COMM_WORLD_RANK:-unknown}"

exec > "${PROJECT_DIR}/genie_output_${JOBTAG}_${RANK}.log" 2>&1

# Now execute the actual AstraSim binary with all arguments passed through
exec "$@"
