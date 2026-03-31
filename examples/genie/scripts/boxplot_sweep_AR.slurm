#!/bin/bash
#SBATCH --job-name=validate_AR
#SBATCH --ntasks=4
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=1
#SBATCH --partition=champollion
#SBATCH --comment='minicol'
#SBATCH --output=outputs/%j/sbatch.out

squeue 

export LD_LIBRARY_PATH=/nfs/jinsun/ibverbs_intercept:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/nfs/jinsun/nccl/build/lib:$LD_LIBRARY_PATH

GENIE_PATH="/nfs/jinsun/astra-sim"

OUTPUT_PATH="${GENIE_PATH}/outputs/${SLURM_JOB_ID}"

echo "### Running the sweep ###"
cd ${GENIE_PATH}
module load openmpi
OUTPUT_PATH="$OUTPUT_PATH" bash run_sweep.sh

echo "### Drawing the graphs ###"
python ${GENIE_PATH}/examples/genie/scripts/run_sweep.py \
    --construct_config True \
    --genie_output_dirname "$OUTPUT_PATH" \
    --nccl_output_dirname "$OUTPUT_PATH" \
    --title "Slurm ID: ${SLURM_JOB_ID}, Date: $(date +"%m/%d"), AR Duration/ 4 Rank, Default Setting(QP: 2, 1MB Chunk, 4 Slots, Ring, Simple)" \
    --output_dirpath "$OUTPUT_PATH" \
    --fig_filename "boxplot"

python ${GENIE_PATH}/examples/genie/scripts/run_sweep_bw.py \
    --construct_config True \
    --genie_output_dirname "$OUTPUT_PATH" \
    --nccl_output_dirname "$OUTPUT_PATH" \
    --title "Slurm ID: ${SLURM_JOB_ID}, Date: $(date +"%m/%d"), AR BusBW/ 4 Rank, Default Setting(QP: 2, 1MB Chunk, 4 Slots, Ring, Simple)" \
    --output_dirpath "$OUTPUT_PATH" \
    --fig_filename "boxplot_bw"