#!/bin/bash
set -ex

TIMETAG=$(date +%m%d_%H%M%S)
COLLECTIVE=${COLLECTIVE:-all_reduce}
COLLECTIVE_UPPER=${COLLECTIVE^^}
export ROOT_PATH=${ROOT_PATH:-/nfs/jinsun}
RUN_SCRIPT=${RUN_SCRIPT:-mpi_run_micro.sh}
mkdir -p "${OUTPUT_PATH}"
echo "Output will be saved to ${OUTPUT_PATH}"

export LD_LIBRARY_PATH=${ROOT_PATH}/ibverbs_intercept:$LD_LIBRARY_PATH
module load openmpi

for size in 8 16 32 64 128 256 512 1024 2048; do
    JOBTAG=size_${size} 
    WORKLOAD=${ROOT_PATH}/astra-sim/examples/genie/workload/microbenchmark/30iter/4npus/${COLLECTIVE_UPPER}_${size} \
    JOBTAG=${JOBTAG} \
    bash ${RUN_SCRIPT}
done

mv genie*.json "${OUTPUT_PATH}/"
mv output*.log "${OUTPUT_PATH}/"