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
# Loop over sizes from 2^20 to 2^30
for epoch in range(num_epochs):
    for size_exp in range(17, 30):  # range from 2^20 to 2^30
        comm_size = 2 ** size_exp  # Compute 2^size_exp
        
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
            "--redis_ip 127.0.0.1",
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
                print(f"Size {size_exp}, System {sys_index} finished in {cycles} cycles")
                result_list.append((size_exp, int(sys_index), int(cycles)))
        except subprocess.CalledProcessError as e:
            print(f"Command failed with error: {e}")

        if (rank == '0'):
            subprocess.run('docker run --net=host --rm -it redis redis-cli -h 127.0.0.1 -p 6379 flushall', shell=True, check=True)
        else: 
            time.sleep(1)

for result in result_list:
    print(f"Exp {result[0]}: System {result[1]} finished in {result[2]} cycles")
    with open(f"results_AR_{num_ranks}_{result[1]}.csv", "a") as csv_file:
        csv_file.write(f"{result[0]},{result[1]},{result[2]}\n")