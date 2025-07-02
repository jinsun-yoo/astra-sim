#!/bin/bash

#SBATCH --partition=champollion           # Partition name
#SBATCH --ntasks=4                       # Total number of tasks (MPI processes)
#SBATCH --nodes=4                         # Number of nodes (1 process per node)
#SBATCH --ntasks-per-node=1               # Number of tasks per node
#SBATCH --time=10:00

export PATH=/opt/openmpi/nvidia/bin/:$PATH
echo $SLURM_PROCID
echo "is the proc id"

RDMA_DRIVER=mlx5_0 RDMA_PORT=1 RANK=$SLURM_PROCID bash run.sh


