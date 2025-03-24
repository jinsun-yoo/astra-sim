# Building and running gloo benchmark

## Building Gloo
```
# Clone and install Gloo
cd ../
git clone git@github.com:facebookincubator/gloo.git
cd gloo
mkdir build
cd build
cmake ../ -DBUILD_BENCHMARK=1 -DUSE_IBVERBS=ON -DUSE_NCCL=ON -DUSE_CUDA=ON -DUSE_MPI=ON
make
ls -l gloo/benchmark
```

## Running Gloo benchmark
```
cd ./gloo/benchmark
mpirun -np 16 \
  ./benchmark_cuda \
  --size 16 \
  --prefix 0 \
  --transport ibverbs \
  --elements -1 \
  --iteration-time 1s \
  cuda_allreduce_ring_chunked
```
Here, elements should go from 2^15 to 2^27 (Each element is an int. If we want to test collective of size 2^29 we need 2^27 elements)

# Running NCCL benchmark
## Building NCCL
```
which mpirun
which nvcc 
#(e.g. /usr/local/cuda of /usr/local/cuda/bin/nvcc)
find /usr -name libnccl.so 2>/dev/null 
# (e.g. /usr/lib/x86_64-linux-gnu of /usr/lib/x86_64-linux-gnu/libnccl.so)

make \
  MPI=1 \
  MPI_HOME=/path/to/mpi \
  CUDA_HOME=/path/to/cuda \
  NCCL_HOME=/path/to/nccl
```

## Running NCCL
```

mpirun \
  -np 16 \
  -N 16 \
  ./build/all_reduce_perf \
  -b 128K \
  -e 1G \
  -f 2 \
```

# Running Gent Emulation
Please refer to [[README.md]]

# Deprecated: Redis
It is preferred to use MPI instead of Redis as the rendezvous, but leaving this below as reference. 
## Running Standalone Redis

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
## Building Gloo
```
HIREDIS_ROOT_DIR=/home/${USER}/.local/
# Clone and install hiredis
git clone git@github.com:redis/hiredis.git
cd {HIREDIS_REPO_PATH}
make
PREFIX={HIREDIS_ROOT_DIR} make install

# Clone and install Gloo
cd ../
git clone git@github.com:facebookincubator/gloo.git
cd gloo
mkdir build
cd build
cmake ../ -DBUILD_BENCHMARK=1 -DUSE_IBVERBS=ON -DUSE_NCCL=ON -DUSE_CUDA=ON -DUSE_REDIS=1 -DHIREDIS_ROOT_DIR=$HIREDIS_ROOT_DIR
make
ls -l gloo/benchmark
```

## Running Gloo benchmark
```
cd ./gloo/benchmark
./benchmark_cuda \
  --size 16 \
  --rank <index of this machine, starting at 0> \
  --redis-host <Redis host> \
  --redis-port 6379 \
  --prefix 0 \
  --transport tcp \
  --elements -1 \
  --iteration-time 1s \
  cuda_allreduce_ring_chunked

redis-cli flushall
```
Here, elements should go from 2^15 to 2^27 (Each element is an int. If we want to test collective of size 2^29 we need 2^27 elements)