#!/bin/bash
set -x

TIMETAG=$(date +%m%d_%H%M%S)
COLLECTIVE=${COLLECTIVE:-all_reduce}
COLLECTIVE_UPPER=${COLLECTIVE^^}
ROOT_PATH=${ROOT_PATH:-/home/users/yoojinsu}
NCCL_TEST_PATH=${NCCL_TEST_PATH:-${ROOT_PATH}/nccl-stuff/2.28/nccl-tests}
NCCL_PATH=${NCCL_PATH:-${ROOT_PATH}/nccl-stuff/2.28/nccl}
NUM_RANKS=8
mkdir -p "${OUTPUT_PATH}"
echo "Output will be saved to ${OUTPUT_PATH}"

export LD_LIBRARY_PATH=${ROOT_PATH}/ibverbs_intercept:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=${NCCL_PATH}/build/lib:$LD_LIBRARY_PATH
module load openmpi

JOBTAG="${JOBTAG:-$(date +%m%d_%H%M%S)}"
if hostname | grep -q "sith"; then
    MCA_STRING="--mca btl_tcp_if_include bond0"
else
    MCA_STRING=""
fi

for size in 8 16 32 64 128 256 512 1024 2048; do
    JOBTAG=size_${size} 
    NCCL_IB_ADAPTIVE_ROUTING=0 \
    NCCL_IB_HCA=mlx5_0 \
    LD_PRELOAD=${ROOT_PATH}/ibverbs_intercept/libibverbs_intercept.so  \
    IBVERBS_INTERCEPT_EXP_TAG=nccl_ibv_trace_${JOBTAG} \
    mpirun ${MCA_STRING} -np 8 -N 1 ${NCCL_TEST_PATH}/build/${COLLECTIVE}_perf \
    -b ${size}M -e ${size}M -n 30 -w 5 -c 0 > nccl_output_${JOBTAG}.log
done
mv nccl_output_size* "${OUTPUT_PATH}/"
mv nccl_ibv_trace_size*.json "${OUTPUT_PATH}/"
