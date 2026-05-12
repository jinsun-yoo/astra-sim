#!/bin/bash
set -x

TIMETAG=$(date +%m%d_%H%M%S)
ROOT_PATH=${ROOT_PATH:-/nfs/jinsun}
NCCL_TEST_PATH=${NCCL_TEST_PATH:-${ROOT_PATH}/nccl-tests}
NCCL_PATH=${NCCL_PATH:-${ROOT_PATH}/nccl}
mkdir -p "${OUTPUT_PATH}"
echo "Output will be saved to ${OUTPUT_PATH}"

export LD_LIBRARY_PATH=${ROOT_PATH}/ibverbs_intercept:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=${NCCL_PATH}/build/lib:$LD_LIBRARY_PATH
module load openmpi


export NCCL_IB_HCA=mlx5_0
export NCCL_IB_ADAPTIVE_ROUTING=0
export NCCL_P2P_DISABLE=1
export NCCL_P2P_LEVEL=LOC
export NCCL_SHM_DISABLE=1
export LD_PRELOAD=${ROOT_PATH}/ibverbs_intercept/libibverbs_intercept.so
export NUM_RANKS=2
for size in 8 16 32 64 128 256 512 1024 2048; do
    export JOBTAG=size_${size}
    export IBVERBS_INTERCEPT_EXP_TAG=nccl_${JOBTAG}
    mpirun -np 2 -N 1 \
        ${NCCL_TEST_PATH}/build/sendrecv_perf \
            -b ${size}M -e ${size}M -n 30 -w 5 -c 0 \
        > nccl_output_${JOBTAG}.log
done
mv nccl_output_size* "${OUTPUT_PATH}/"
mv nccl_size*.json "${OUTPUT_PATH}/"
