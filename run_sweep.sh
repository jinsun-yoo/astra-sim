#!/bin/bash
set -x

mkdir -p "${OUTPUT_PATH}"
echo "Output will be saved to ${OUTPUT_PATH}"

bash run_sweep_genie.sh
bash run_sweep_nccl.sh