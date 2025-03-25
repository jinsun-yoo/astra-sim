import subprocess
import os
import time
import re

# Define the sample directory path
sample_dir = "/app/astra-sim/examples/gent"

rank = os.getenv('RANK')
print('rank is ', rank)
result_list = []

num_epochs = 10
num_ranks = 4
size_exp = 20
redis_ip = '172.22.186.221'

if (rank == '0'):
    subprocess.run(f'docker run --net=host --rm -it redis redis-cli -h {redis_ip} -p 6379 flushall', shell=True, check=True)
else: 
    time.sleep(1)

# Construct the command with COMM_SIZE and MPI_RANK
command = [
    "docker", "run",
    "--net=host",
    f"--device=/dev/infiniband/uverbs0",
    "--device=/dev/infiniband/rdma_cm",
    "--ulimit", "memlock=-1:-1",
    "-t", "jyoo332/astra-sim-gent-redis", "build/astra_gent/build/bin/AstraSim_Gent",
    f"--workload {sample_dir}/workload/microbenchmark/ALL_REDUCE_{size_exp}",
    f"--system {sample_dir}/system.json",
    f"--memory {sample_dir}/remote_memory.json",
    f"--logical_topology {sample_dir}/logical_topology_{num_ranks}.json",
    f"--mpi_rank {rank}",
    f"--rdma_driver mlx5_0",
    "--rdma_port 1",
    f"--redis_ip {redis_ip}",
    f"--num_ranks {num_ranks}"
]

# Run the command using subprocess
try:
    print(f"Running: {' '.join(command)}")  # Print the command for visibility
    s = subprocess.run(" ".join(command), shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
    output = s.stdout
    matches = re.findall(r"sys\[(\d+)\] finished, (\d+) cycles", output)
    for match in matches:
        sys_index, cycles = match
        print(f"System {sys_index} finished in {cycles} cycles")
    with open(f"stdout_{rank}.txt", "w") as file:
        file.write(output)
    with open(f"stderr_{rank}.txt", "w") as file:
        file.write(s.stderr)
except subprocess.CalledProcessError as e:
    print(f"Command failed with error: {e}")
    with open(f"stdout_{rank}.txt", "w") as file:
        file.write(output)
    with open(f"stderr_{rank}.txt", "w") as file:
        file.write(s.stderr)

if (rank == '0'):
    subprocess.run(f'docker run --net=host --rm -it redis redis-cli -h {redis_ip} -p 6379 flushall', shell=True, check=True)
else: 
    time.sleep(1)
