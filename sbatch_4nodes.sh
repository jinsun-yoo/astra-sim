#!/bin/bash
#SBATCH --job-name=Gent_Perf_AllReduce_4Node    # Job name
#SBATCH --partition=champollion           # Partition name
#SBATCH --ntasks=4                       # Total number of tasks (MPI processes)
#SBATCH --nodes=4                         # Number of nodes (1 process per node)
#SBATCH --ntasks-per-node=1               # Number of tasks per node
#SBATCH --time=01:00:00                   # Maximum runtime (HH:MM:SS)
#SBATCH --output=gent_perf_allreduce_4node_%j.log   # Output file for logs (with job ID)
#SBATCH --error=gent_perf_allreduce_4node_%j.err    # Error file for logs (with job ID)
 
# Load necessary modules (if any are required for your environment, like MPI)
#module load mpi
export NCCL_IB_HCA=ib0
#export NCCL_SOCKET_IFNAME=ib0
export NCCL_DEBUG=INFO
#export NCCL_NET_PLUGIN=/opt/nvidia/hpc_sdk/Linux_x86_64/2025/comm_libs/nccl/lib/
export LD_LIBRARY_PATH=/opt/openmpi/nvidia/lib/:$LD_LIBRARY_PATH 

# Run the mpirun command with your specified options
/opt/openmpi/nvidia/bin/mpirun \
  -np $SLURM_NTASKS \
  -N 1 \
  docker run \
  --rm \
  --network=host \
  --device=/dev/infiniband/uverbs0 \
  --device=/dev/infiniband/rdma_cm \
  --ulimit memlock=-1:-1 \
  -e RDMA_PORT=1 \
  -e RDMA_DRIVER=mlx5_0 \
  -t jyoo332/astra-sim-gent:mpi_0324 \
  bash ./mpi_run.sh