# Building Gent
## Dependencies
```
sudo apt-get install libibverbs-dev rdma-core
sudo apt-get install protobuf-compiler
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


## Building Gloo Dependency
Gloo will be built along the build script. 

## Actually building gent
```
bash build/astra_gent/build.sh
```
To clean, 
```
bash build/astra_gent/build.sh -l
```

# Running Gent
Refer to `run.sh`. Depending on how you execute them (slurm v. mpi v. etc.), adjust as necessary

- `rdma_driver`: e.g. `mlx5_0`
- `rdma_port`: the port number obtained after rdma_driver in `rdma link`
- `redis_ip`: The redis server IP. Refer to below.

## Redis
Gloo uses a Redis server as the Rendezvous backend. The easiest way to run a redis server is to use a docker container.
The configuration file used below exposes ALL IP addresses for redis-server to listen to. Adjust as necessary.
```
docker run -v ./astra-sim/network_frontend/gent/redis.conf:/redis-stack.conf -p 6379:6379 -p 8001:8001 redis/redis-stack:latest
```

Between each run of redis, the redis storage needs to be flushed. This is a rather tedious job that will later be automated
```
redis-cli -h ${REDIS_IP} flushall
```