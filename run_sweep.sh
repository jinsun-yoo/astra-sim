#!/bin/bash
set -x

TIMETAG=$(date +%m%d_%H%M%S)
COLLECTIVE=${COLLECTIVE:-all_reduce}
COLLECTIVE_UPPER=${COLLECTIVE^^}
NCCL_TEST_PATH="/nfs/jinsun/nccl-tests"
mkdir -p "${OUTPUT_PATH}"
echo "Output will be saved to ${OUTPUT_PATH}"

export LD_LIBRARY_PATH=/nfs/jinsun/ibverbs_intercept:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/nfs/jinsun/nccl/build/lib:$LD_LIBRARY_PATH
module load openmpi

for size in 8 16 32 64 128 256 512 1024 2048; do
    JOBTAG=size_${size} 
    WORKLOAD=/nfs/jinsun/astra-sim/examples/genie/workload/microbenchmark/30iter/4npus/${COLLECTIVE_UPPER}_${size} \
    JOBTAG=${JOBTAG} \
    bash mpi_run_micro.sh
done

mv output_size* "${OUTPUT_PATH}/"
mv genie_ibv_trace_size* "${OUTPUT_PATH}/"

for size in 8 16 32 64 128 256 512 1024 2048; do
    JOBTAG=size_${size} 
    NCCL_IB_ADAPTIVE_ROUTING=0 \
    NCCL_IB_HCA=mlx5_0 \
    LD_PRELOAD=/nfs/jinsun/ibverbs_intercept/libibverbs_intercept.so  \
    IBVERBS_INTERCEPT_EXP_TAG=nccl_${JOBTAG} \
    mpirun -np 4 -N 1 ${NCCL_TEST_PATH}/build/${COLLECTIVE}_perf \
    -b ${size}M -e ${size}M -n 30 -w 0 -c 0 > nccl_output_${JOBTAG}.log
done
mv nccl_output_size* "${OUTPUT_PATH}/"
mv nccl_size*.json "${OUTPUT_PATH}/"