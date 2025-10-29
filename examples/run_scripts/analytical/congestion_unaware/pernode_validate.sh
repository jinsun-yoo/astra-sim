#!/bin/bash
set -e

## ******************************************************************************
## This source code is licensed under the MIT license found in the
## LICENSE file in the root directory of this source tree.
##
## Copyright (c) 2024 Georgia Institute of Technology
## ******************************************************************************

# find the absolute path to this script
SCRIPT_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR="${SCRIPT_DIR:?}/../../../.."
EXAMPLE_DIR="${PROJECT_DIR:?}/examples"

# paths
ASTRA_SIM="${PROJECT_DIR:?}/build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Unaware"
WORKLOAD="${EXAMPLE_DIR:?}/workload/pernode_validate"
SYSTEM="${EXAMPLE_DIR:?}/system/custom_collectives/pernode_with_global.json"
NETWORK="${EXAMPLE_DIR:?}/network/analytical/Ring_Switch_8npus.yml"
REMOTE_MEMORY="${EXAMPLE_DIR:?}/remote_memory/analytical/no_memory_expansion.json"
COMM_GROUP="${EXAMPLE_DIR:?}/comm_group/pernode_8npus_commgroup.json"

# start
echo "[ASTRA-sim] Compiling ASTRA-sim with the Analytical Network Backend..."
echo ""

# Compile
echo ""
echo "[ASTRA-sim] Running ASTRA-sim Example with Analytical Network Backend..."
echo ""

# run ASTRA-sim
"${ASTRA_SIM:?}" \
    --workload-configuration="${WORKLOAD}" \
    --system-configuration="${SYSTEM:?}" \
    --remote-memory-configuration="${REMOTE_MEMORY:?}" \
    --network-configuration="${NETWORK:?}" \
    --comm-group-configuration="${COMM_GROUP:?}"

# finalize
echo ""
echo "[ASTRA-sim] Finished the execution."
