# Genie Userguide

## TL, DR:
```bash
salloc -N 4
module load openmpi # or whatever module/command you need to activate MPI
bash build/astra_genie/build.sh
bash examples/genie/mpi_run.sh
```

## Dependencies
Genie relies on **Gloo**, which abstracts away the task of building/registering RDMA QPs, registering MR, sending point-to-point messages (by calling ibv_post_send), etc. Gloo's implementation requires a method for different instances to discover each other (i.e. rendezvous). This guide assumes **MPI** over **SLURM**. You can alternate between different rendezvous options by configuring 'astra-sim/build/astra_genie/CMakeLists.txt'

A **BIG** assumption is that the same `astra-sim` directory is accessible by all SLURM nodes, perhaps through a network filesystem. I have not tried building Genie (at least recently) on an environment where there is no common network filesystem.

### Gloo
Genie locally clones Gloo as a submodule, and builds Gloo directly from source. 
The cmake files already have the instructions to build Gloo from source & link it as a library. 
Just run `git submodule init && git submodule update` to ensure Gloo is cloned. 

### MPI
MPI should be installed by the SLURM cluster manager. If you do not have MPI installed, please refer to the Redis chapter at the end of this document. 

## Building and Running

### Building
```bash
salloc -N 4 -p <partition name>
# From the first node you are allocated
module load openmpi
# The root directory of this repo, not astra-sim/astra-sim
cd astra-sim 
bash build/astra_genie/build.sh
```

### Running
```bash
bash examples/genie/mpi_run.sh
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

### Input/Output
#### Input
- **Chakra Graph:**
This will be the same as the normal ASTRA-sim usecase. Current use has been focused on collecting post-execution traces and playing it through Genie just for the sake of validating Genie's behavior. It is possible to use pre-execution, synthetic traces to test scenarios where you don't have the GPUs but you do have the CPU-only host machines.

- **System layer, logical topology, comm_group input:**
Genie does not need a description of the *physical* topology. The instances will figure out each other through MPI, etc. However, the *logical* topology (and any other inputs)is needed for the *System* layer to break down the collectives into point-to-point send/receive messages. This behavior of the System layer (breakdown to p2p) is irrespective asto whether we use Analytical or Genie backend.

- **RDMA_Driver, RDMA_PORT**: Currently set as a default value. Feel free to extend. This needs to be revisited especially we model more-than-one NIC (=more-than-couple-ranks) per host machine.

#### Output
- **ASTRA-sim default output**
The default outputs, etc. remain same. The only difference is that ASTRA-sim will now report wallclock duration, not the simulated duration. 

- **(OPTIONAL) Chrometrace**
A separate plugin that generates Perfetto traces (JSON file), which allows us to easily visualize the Genie timeline. To enable, when buildng, 

The variable `CHROMETRACE` in `astra-sim/CMakeLists.txt` determines how granular chrometrace to capture.
- NONE or not defined Do not generate any chrometrace file
- WORKLOAD: Mark the beginning and end of each Chakra Node. (e.g. beginning/end of an AllReduce operator)
- EVENT_QUEUE: Mark the beginning and end of each events as processed by the event queue.

The granularity is set by setting the `GENIE_CHROMETRACE_WORKLOAD` and `GENIE_CHROMETRACE_EVENT` at *compile time*. Note the CMake function `target_compile_definitions` is equivalent to setting `#define`. We set this as compile time, because we want to minimize the overhead caused by Chrometraces, which means no branching, which means everything is fixed in complile time. 

The `CHROMETRACE` and `CHROMETRACE_SIZE` envvars, if set, will override whichever value is written in ChromeTraces.txt.
```bash
CHROMETRACE=WORKLOAD bash build/astra_genie/build.sh
# OR
CHROMETRACE=EVENT_QUEUE CHROMETRACE_SIZE=1048576 bash build/astra_genie/build.sh
# OR
unset CHROMETRACE; bash build/astra_genie/build.sh
```


- **(OPTIONAL) Infiniband trace**
Similar to the chrometrace, generates a separate Perfetto trace (JSON file) of all invocations to the ibverbs API, and the timeline. This can be used to see stuff like 1) whether Genie is creating messages with the right data size & 2) number of QP (are all messages between two hosts sent to one QP only or are all messages sent across multiple QPs?) 3) Genie is making the verbs API call at the right time. 

The infiniband tracer is implemented as a separate shared library. We use LD_PRELOAD so that when resolving calls to the verbs API, the infiniband tracer-shared library will come before `libibverbs.so`. The shared library will record the calls to a separate data structure, and then make the call to the actual API function. At the end of the program, the calls will be flushed to a JSON file. 

```bash
git clone git@github.com:jinsun-yoo/ibverbs_intercept.git
make all 
cd ${ASTRA_SIM_DIR}
export LD_PRELOAD=${FILEPATH}/libibverbs_intercept.so
# Alternatively, this envvar export is already available in mpi_run.sh
bash mpi_run.sh
```

This outputs two sets of files: `raw_trace_$i.json` holds all the raw data, `trace_$i.json` holds the file in chrometrace format.

## Software Structure
The key difference in Genie is to add a *network backend* to ASTRA-sim (i.e. that replases the Analytical/NS-3 network backends). We launch ASTRA-sim with the key inputs (Chakra graphs, system input file, etc) and let it do its job (traversing through workload, breaking down collectives into send/receive, etc.). When it calls `sim_send`, `sim_recv`, and `sim_schedule`, things start to change. Here are the key differences between ASTRA-sim with the Genie network backend and ASTRA-sim with a simulation backend (e.g. Analytical)

- At `main.cc`, when instantiating the network backend: 
    - `new AnalyticalNetworkBackend` reads the network topology file, and sets up internal data structures
    - `new GenieNetworkBackend` calls Gloo's respective setup APIs, which use MPI to discover different instances, communicate with each other, sets up RDMA QP and MRs, changes the state of the RDMA QP to RTR, etc. 

- At `sim_schedule`, 
    - The Analytical backend when simulated time x passes, it will call the callback function back into ASTRA-sim 
    - The Genie backend will poll until the actual wall clock time has passed, then call the callback function back into ASTRA-sim.

- At `sim_send`/`sim_recv`,
    - The Analytical backend will call, and simulate the duration. Similarly, when simulated time x passes, it will call the callback function back into ASTRA-sim 
    - The Genie backend will call `ibv_post_send`. it will periodically poll
