# %%
import re
import matplotlib.pyplot as plt
import argparse
import sys
from pathlib import Path

#%%
timetag = "0326_0618"
genie_dir = "/nfs/jinsun/astra-sim"
output_dirname = f"{genie_dir}/outputs/045610"
configname = 'boxplot_0326_0618'

class PlotConfig:
    def __init__(self, title, genie_output_dirname, nccl_output_dirname, fig_filename, output_dirpath = ""):
        self.title = title
        self.genie_output_dirname = genie_output_dirname
        self.nccl_output_dirname = nccl_output_dirname
        self.fig_filename = fig_filename
        self.output_dirpath = output_dirpath

plot_configs = { 
    '0326_0618': PlotConfig(
               title='Date: AR / 4 Rank, QP:2, 1MB Chunk, 4Slots, Ring, Simple', 
               genie_output_dirname=f"{genie_dir}/outputs/045610", 
               nccl_output_dirname=f"{genie_dir}/outputs/045610",
               fig_filename='boxplot_0326_0618',
               ),
}

# %%
def get_boxplot_data(log_dirname: str):
    genie_data = {}
    for size_pow in range (3, 12):
        for iteration in range (10):
            coll_size = pow(2, size_pow)
            filename = f"{log_dirname}/output_size_{coll_size}_iter_{iteration}.log"
            with open(filename, 'r') as f:
                content = f.read()
                matches = re.findall(r'([0-9]+(?:\.[0-9]+)?)\s+Gbps', content)
                if matches:
                    bw_list = []
                    sum_num = 0
                    for m in matches:
                        bw_list.append(float(m))
                        sum_num += float(m)
                    average = sum_num / len(bw_list)
                    print(f"Size: {coll_size}, Iter: {iteration}, Cycles: {bw_list}, Average: {average}")
                    if coll_size not in genie_data:
                        genie_data[coll_size] = []
                    genie_data[coll_size].append(average)
                else:
                    print(f"Size: {coll_size}, Iter: {iteration}, No match found")
    sorted_sizes_genie = sorted(genie_data.keys())
    plot_data_genie = [genie_data[size] for size in sorted_sizes_genie]
    avg_genie = [sum(genie_data[size]) / len(genie_data[size]) for size in sorted_sizes_genie]
    return plot_data_genie, avg_genie

# %%
def get_line_data(log_dirname: str):
    nccl_multirun_data =[] 
    for size_pow in range (3, 12):
        coll_size = pow(2, size_pow)
        filename = f"{log_dirname}/nccl_output_size_{coll_size}.log"
        with open(filename, 'r') as f:
            content = f.read()
            pattern = r'^\s*\d+\s+\d+\s+\S+\s+\S+\s+\S+\s+\d+(?:\.\d+)?\s+\d+(?:\.\d+)?\s+(\d+(?:\.\d+)?)\s+(?:\S+)?\s*$'
            matches = re.findall(pattern, content, re.MULTILINE)
            if matches:
                if len(matches) != 1:
                    print(f"Warning: Expected 1 match for size {coll_size}, but found {len(matches)}. Using the first match.")
                busbw = float(matches[0])
                nccl_multirun_data.append(busbw * 8)
                print(f"Size: {coll_size}, NCCL busbw: {busbw}")
            else:
                print(f"Size: {coll_size}, No match found")
    return nccl_multirun_data

# %%
def draw_plot(config: PlotConfig):
    # Plot the data
    fig, ax = plt.subplots(figsize=(12, 6))

    output_dirname = config.output_dirpath if config.output_dirpath else f"{Path(__file__).resolve().parent}/generated_graphs"
    fig_filename = config.fig_filename if config.fig_filename else f"duration_boxplot"


    genie_boxplot, genie_avg = get_boxplot_data(config.genie_output_dirname)
    bp_genie = ax.boxplot(genie_boxplot, positions=range(len(genie_avg)), widths=0.6, patch_artist=True, boxprops=dict(facecolor='lightblue'))
    ax.plot(range(len(genie_avg)), genie_avg, 'o-', color='blue', linewidth=2, markersize=6, label='Genie Average')

    nccl_multirun_data = get_line_data(config.nccl_output_dirname)
    ax.plot(range(len(nccl_multirun_data)), nccl_multirun_data, 'd-', color='green', linewidth=2, markersize=6, label='NCCL-test Average')

    ax.set_xlabel('Collective Size (MB)', fontsize=18)
    ax.set_ylabel('BusBW (Gbps))', fontsize=18)
    ax.set_title(config.title, fontsize=20)
    ax.tick_params(axis='y', labelsize=14)
    ax.tick_params(axis='x', labelsize=14)
    ax.grid(True, alpha=0.3)
    ax.legend()

    coll_size = [pow(2, size_pow) for size_pow in range (3, 12)]
    plt.xticks(range(len(genie_avg)), coll_size, rotation=45)
    plt.tight_layout()
    plt.savefig(f"{output_dirname}/{fig_filename}.png", dpi=600)
    print(f"Plot saved to {output_dirname}/{fig_filename}.png")
    plt.savefig(f"{output_dirname}/{fig_filename}.pdf", metadata={'Title': timetag}, dpi=600)
    print(f"Plot saved to {output_dirname}/{fig_filename}.pdf")

# %%
import sys
if __name__ == "__main__" and "ipykernel" not in sys.modules:
    args = argparse.ArgumentParser(description='Plotting script for Genie and NCCL performance comparison')
    args.add_argument('--timetag', type=str, default="", help='The timetag corresponding to the output directories for Genie and NCCL logs')
    args.add_argument('--construct_config', type=bool, default=False, help='Construct config from command line arguments if true')
    args.add_argument('--title', type=str, help='Title for the plot')
    args.add_argument('--genie_output_dirname', type=str, help='Directory name for Genie output logs')
    args.add_argument('--nccl_output_dirname', type=str, help='Directory name for NCCL output logs')
    args.add_argument('--fig_filename', type=str, help='Filename for the saved figure (without extension)')
    args.add_argument('--output_dirpath', type=str, default="", help='Directory path to save the generated figure. If empty, it will be saved in a default "generated_graphs" directory next to this script.')

    args = args.parse_args()
    if args.construct_config:
        if args.timetag != "":
            print(f"Error: --timetag should be empty when --construct_config is true. Please provide the necessary parameters to construct the config.")            
            exit(1)
        config = PlotConfig(
            title=args.title,
            genie_output_dirname=args.genie_output_dirname,
            nccl_output_dirname=args.nccl_output_dirname,
            fig_filename=args.fig_filename,
            output_dirpath=args.output_dirpath
        )
        draw_plot(config)
        exit(0)
    timetag = args.timetag
configname = configname if configname else timetag
config = plot_configs.get(configname)
if config is None:
    print(f"Error: No plot configuration found for timetag '{timetag}'. Please check the available configurations.")
else:
    draw_plot(config)


# %%