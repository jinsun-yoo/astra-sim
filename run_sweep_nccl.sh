#!/bin/bash
set -x

TIMETAG=$(date +%m%d_%H%M%S)
COLLECTIVE=${COLLECTIVE:-all_reduce}
COLLECTIVE_UPPER=${COLLECTIVE^^}
ROOT_PATH=${ROOT_PATH:-/nfs/jinsun}
NCCL_TEST_PATH=${NCCL_TEST_PATH:-${ROOT_PATH}/nccl-tests}
NUM_RANKS=4
mkdir -p "${OUTPUT_PATH}"
echo "Output will be saved to ${OUTPUT_PATH}"

export LD_LIBRARY_PATH=${ROOT_PATH}/ibverbs_intercept:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=${ROOT_PATH}/nccl/build/lib:$LD_LIBRARY_PATH
module load openmpi


for size in 8 16 32 64 128 256 512 1024 2048; do
    JOBTAG=size_${size} 
    NCCL_IB_ADAPTIVE_ROUTING=0 \
    NCCL_IB_HCA=mlx5_0 \
    NCCL_P2P_DISABLE=1 \
    LD_PRELOAD=${ROOT_PATH}/ibverbs_intercept/libibverbs_intercept.so  \
    IBVERBS_INTERCEPT_EXP_TAG=nccl_${JOBTAG} \
    mpirun -np 4 -N 1 ${NCCL_TEST_PATH}/build/${COLLECTIVE}_perf \
    -b ${size}M -e ${size}M -n 30 -w 5 -c 0 > nccl_output_${JOBTAG}.log
done
mv nccl_output_size* "${OUTPUT_PATH}/"
mv nccl_size*.json "${OUTPUT_PATH}/"
