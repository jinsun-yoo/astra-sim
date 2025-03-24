# Building Gent
## Using a docker container + slurm + mpirun
```
# Single run, 4 nodes
sbatch ./sbatch_4nodes.sh

# Loop for many data sizes on 4 nodes
sbatch ./sbatch_loop_4nodes.sh

# Single run, 16 nodes
sbatch ./sbatch_16nodes.sh

# Loop for many data sizes on 16 nodes
sbatch ./sbatch_loop_16nodes.sh
```

## Building and running Gent locally
### Dependencies
The following dependencies must be installed: 
- Protobuf compiler. Because of this alone it is recommended to run Gent inside a Docker container. 
- RDMA related packages, including but not limited to:
```
sudo apt-get install libibverbs-dev rdma-core
```
- Gloo: There is nothing to do. Gloo is cloned as a submodule and built together with the build script. 

## Actually building gent
```
CMAKE_ARGS=-DUSE_MPI=1 bash build/astra_gent/build.sh
```
To clean, 
```
bash build/astra_gent/build.sh -l
```

## Running Gent
The runtime flags that Gent requires are as follows:
- `rdma_driver`: e.g. `mlx5_0`
- `rdma_port`: the port number obtained after rdma_driver in `rdma link`
- `redis_ip`: The redis server IP. Refer to below.

One could also use the helper script `${ASTRA_SIM_DIR}/run.sh`. Within the docker container, it would look like this:
```
docker run \
  --net=host \
  --device=/dev/infiniband/uverbs0 \
  --device=/dev/infiniband/rdma_cm \
  --ulimit memlock=-1:-1 \
  -e RDMA_DRIVER=mlx5_0 \
  -e MPI_RANK=0 \
  -e RDMA_PORT=1 \
  -e REDIS_IP=192.168.1.1 \
  -t jyoo332/astra-sim-gent bash run.sh
```



# Deprecated: Redis
It is preferred to use MPI compared to Redis. However, leaving this info here for reference.
## Prerequisites: Redis
Gent uses Gloo, which uses Redis as the rendezvous backend for collective communications. 
The recommended way to run Redis is inside a standalone container: 

> Warning: The existing redis-stack.conf listens to port 6379 of ALL addresses. Change the following line to bind a specific IP address to the server
```
bind * -::* 
```

This is the command to run Redis in a standalone container.

```
docker run \
    -v ${ASTRA_SIM_DIR}/astra-sim/network_frontend/gent/redis.conf:/redis-stack.conf \
    -p 6379:6379 \
    -p 8001:8001 \
    -d redis/redis-stack:latest
```
Between each run of redis, the redis storage needs to be flushed. This is a rather tedious job that will later be automated
```
redis-cli -h ${REDIS_IP} flushall
```
## Running Gent inside a docker container (recommended)
Refer to the following command. For an explanation on each flags, refer to the section "Running Gent" below:

> WARNING: The --device flag to docker, and the --mpi_rank, rdma_driver, rdma_port flag to ASTRA-sim should change depending on the node / device mounts

```
docker run \
    --net=host \
    --device=/dev/infiniband/uverbs0 \
    --device=/dev/infiniband/rdma_cm \
    --ulimit memlock=-1:-1 \
    -t jyoo332/astra-sim-gent build/astra_gent/build/bin/AstraSim_Gent \
        --workload /app/astra-sim/examples/gent/workload/AllReduce_1MB \
        --system /app/astra-sim/examples/gent/system.json \
        --memory /app/astra-sim/examples/gent/remote_memory.json \
        --logical_topology /app/astra-sim/examples/gent/logical_topology_16.json \
        --mpi_rank 0 \
        --rdma_driver mlx5_0 \
        --rdma_port 1 \
        --redis_ip 127.0.0.1 \
        --num_ranks 16
```
Or, alternatively, 
```
docker build -t astra-sim-gent .
```

## Building Gent locally
Additional dependencies
- libhiredis: A C++ library that Gloo uses to interact with the Redis server. It is possible to clone & build hiredis locally. In that case, refer to "Building Hiredis Locally" below:
```
sudo apt-get install libhiredis-dev
```
## Actually building gent
```
CMAKE_ARGS=-DUSE_REDIS=1 bash build/astra_gent/build.sh
```
To clean, 
```
bash build/astra_gent/build.sh -l
```

## Running Gent
The runtime flags that Gent requires are as follows:
- `rdma_driver`: e.g. `mlx5_0`
- `rdma_port`: the port number obtained after rdma_driver in `rdma link`
- `redis_ip`: The redis server IP. Refer to below.

One could also use the helper script `${ASTRA_SIM_DIR}/run.sh`. Within the docker container, it would look like this:
```
docker run \
  --net=host \
  --device=/dev/infiniband/uverbs0 \
  --device=/dev/infiniband/rdma_cm \
  --ulimit memlock=-1:-1 \
  -e RDMA_DRIVER=mlx5_0 \
  -e MPI_RANK=0 \
  -e RDMA_PORT=1 \
  -e REDIS_IP=192.168.1.1 \
  -t jyoo332/astra-sim-gent bash run.sh
```

## Building Hiredis Dependency
### Installing with sudo
```
sudo apt-get install libhiredis-dev
```
### Building locally
Following the instructions 
```
git clone git@github.com:redis/hiredis.git
cd hiredis
make
make PREFIX=$HOME/.local install
```

Ideally, hiredis should be installed in `$HOME/.local/lib`, etc. Refer to `astra-sim/build/astra_gent/CMakeLists.txt:30-32`, and change the paths as necessary

If building locally, export the following environment variable for CMakeLists.txt to correctly locate hiredis.:
```
export INSTALL_HIREDIS_LOCALLY="True"
```