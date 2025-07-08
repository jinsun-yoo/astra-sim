# Building & Running Genie

Genie relies on Gloo, which performs a one-time rendezvous operation for the different instances to discover each other. 
Gloo supports several rendezvous options, but this guide assumes using MPI over SLURM. 
You can alternate between different rendezvous options by configuring 'astra-sim/build/astra_genie/CMakeLists.txt'

## Dependencies.
A **BIG** assumption is that the same `astra-sim` directory is accessible by all SLURM nodes, perhaps through a network filesystem. I have not tried building Genie (at least recently) on an environment where there is no common network filesystem.

Clone ASTRA-sim *into a common network filesystem* and install all other dependencies for ASTRA-sim. 
On top of them, you need 1. Gloo and 2. MPI. 

**Gloo** Genie locally clones Gloo as a submodule, and builds Gloo directly from source. 
The cmake files already have the instructions to build Gloo from source & link it as a library. 
Just run `git submodule init && git submodule update` to ensure Gloo is cloned. 

## Building
```bash
salloc -N 4 -p <partition name>
# From the first node you are allocated
module load openmpi
# The root directory of this repo, not astra-sim/astra-sim
cd astra-sim 
bash build/astra_genie/build.sh
```

## Running
```bash
bash mpi_run.sh
```

Which evaluates to 
```bash
mpirun -np 4 -n 1\
./build/astra_gent/build/bin/AstraSim_Genie \
--workload {PARENT_DIR}/astra-sim/examples/genie/workload/ALL_GATHER \
--system {PARENT_DIR}/astra-sim/examples/genie/system.json \
--memory {PARENT_DIR}/astra-sim/examples/genie/remote_memory.json \
--logical_topology {PARENT_DIR}/astra-sim/examples/genie/logical_topology_4.json \ 
--rdma_driver mlx5_0 \
--rdma_port 1
```