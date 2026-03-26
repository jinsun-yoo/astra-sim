#!/bin/bash
set -ex

TIMETAG=$(date +%m%d_%H%M%S)
OUTPUT_PATH=${OUTPUT_PATH:-outputs/${TIMETAG}}
NCCL_TEST_PATH="/nfs/jinsun/nccl-tests"
mkdir -p "${OUTPUT_PATH}"
echo "Output will be saved to ${OUTPUT_PATH}"

export LD_LIBRARY_PATH=/nfs/jinsun/ibverbs_intercept:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/nfs/jinsun/nccl/build/lib:$LD_LIBRARY_PATH
# export LD_LIBRARY_PATH=/nfs/jinsun/nccl/ext-profiler/example:$LD_LIBRARY_PATH
module load openmpi

for size in 8 16 32 64 128 256 512 1024 2048; do
    for iteration in {0..10}; do
        JOBTAG=size_${size}_iter_${iteration} 
        GENIE_SIMPLERING_COLLECTIVE_SIZE_MB=${size} \
        JOBTAG=${JOBTAG} \
        bash mpi_run_micro.sh
    done
done

mv output_size* "${OUTPUT_PATH}/"
mv genie_ibv_trace_size* "${OUTPUT_PATH}/"

for size in 8 16 32 64 128 256 512 1024 2048; do
    JOBTAG=size_${size} 
    NCCL_IB_ADAPTIVE_ROUTING=0 \
    LD_PRELOAD=/nfs/jinsun/ibverbs_intercept/libibverbs_intercept.so  \
    IBVERBS_INTERCEPT_EXP_TAG=nccl_${JOBTAG} \
    NCCL_PROFILER_DUMP_FILE=nccl_${JOBTAG} \
    mpirun -np 4 -N 1 ${NCCL_TEST_PATH}/build/all_reduce_perf \
    -b ${size}M -e ${size}M -n 10 -w 0 -c 0 > nccl_output_${JOBTAG}.log
done
mv nccl_output_size* "${OUTPUT_PATH}/"
mv nccl_size*.json "${OUTPUT_PATH}/"