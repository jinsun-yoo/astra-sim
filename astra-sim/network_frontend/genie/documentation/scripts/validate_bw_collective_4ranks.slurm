#!/bin/bash
#SBATCH --job-name=genie_workload_run
#SBATCH --ntasks=4
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=1
#SBATCH --partition=champollion
#SBATCH --output=/nfs/jinsun/astra-sim/dummy_runs/outputs/%j/sbatch.out
#SBATCH --time=00:30:00

# Prolog
## Set bash environment
set -ex
squeue -p champollion
module load openmpi
# source "${ROOT_PATH}/venv/genie/bin/activate"

## Get OUTPUT_PATH from sbatch directive. This is where all intermediate/final files will go to.
STDOUT_PATH=$(scontrol show job -o "$SLURM_JOB_ID" \
  | tr ' ' '\n' \
  | awk -F= '$1=="StdOut"{print $2}')
OUTPUT_PATH=$(dirname "$STDOUT_PATH")
mkdir -p "${OUTPUT_PATH}"
export OUTPUT_PATH
cp "$0" "${OUTPUT_PATH}/"


## Set Envvars
### Envvars needed in this script.
ROOT_PATH=${ROOT_PATH:-/nfs/jinsun}
ASTRASIM_PATH=${ROOT_PATH}/astra-sim
GENIE_SCRIPT=${ASTRASIM_PATH}/mpi_run_micro.sh
NUM_RANKS=4

export NUM_RANKS

### Envvars needed in children scripts
SCRIPT_DIR="${ROOT_PATH}/torchtitan"
RDZV_MASTER_HOSTNAME="$(scontrol show hostnames "$SLURM_NODELIST" | head -n 1)"
JOB_CONFIG_NAME="8b_fsdp"
NRANK_PER_NODE=1
NNODES=4
NUM_RANKS=$((NRANK_PER_NODE * NNODES))
CONFIG_FILE="${ROOT_PATH}/rfsdp_root/toy_experiments/torchtitan_profile_alltoall/configs/llama3_${JOB_CONFIG_NAME}.toml"
if [[ ! -f "${CONFIG_FILE}" ]]; then
  CONFIG_FILE="${ROOT_PATH}/rfsdp_root/alltoall_prototype_compare/configs/llama3_${JOB_CONFIG_NAME}.toml"
fi
MODEL_NAME="llama3"

export OUTPUT_PATH ROOT_PATH SCRIPT_DIR RDZV_MASTER_HOSTNAME JOB_CONFIG_NAME NRANK_PER_NODE NNODES NUM_RANKS CONFIG_FILE MODEL_NAME 

### Cluster-specific envvars
export NCCL_P2P_LEVEL=LOC
export NCCL_P2P_DISABLE=1
export NCCL_IB_DISABLE=0


## Start monitoring tools
### Minicol
cd $OUTPUT_PATH
start-minicol
cd -

### Self-written bw-monitor
# if [[ -f /tmp/bw_monitor.pid ]]; then
#   PID="$(cat /tmp/bw_monitor.pid 2>/dev/null || true)"
#   if [[ -n "$PID" ]] && kill -0 "$PID" 2>/dev/null; then
#     echo "bw_monitor already running (PID ${PID}); reusing existing monitor."
#   else
#     rm -f /tmp/bw_monitor.pid
#     mpirun -np $NNODES -N 1 bw-start "${OUTPUT_PATH}"
#   fi
# else
#   mpirun -np $NNODES -N 1 bw-start "${OUTPUT_PATH}"
# fi

# Actual Run
## Run the torchtitan workload to generate trace files
# cd "${SCRIPT_DIR}"
# bash run_train_for_collect.sh

# ## Combine the trace files into a Chakra trace.
# bash combine_trace.sh

## Run Genie on the output trace files.
JOBTAG=${POSTFIX} \
WORKLOAD=${ASTRASIM_PATH}/examples/genie/workload/microbenchmark/30iter/4npus/ALL_GATHER_1024 \
ENABLE_IBVERBS_INTERCEPT=1 \
bash ${GENIE_SCRIPT} 

## Run parsers to plot the output.

# Epilog
## Stop the handwritten bandwidth monitoring tool.
# mpirun -np 4 -N 1 bw-stop
## Stop the manually run minicol tool.
stop-minicol
