import argparse
import os

from chakra.schema.protobuf.et_def_pb2 import * 

from chakra.schema.protobuf.et_def_pb2 import (
    AttributeProto as ChakraAttr,
)
from chakra.schema.protobuf.et_def_pb2 import (
    Node as ChakraNode,
)
from chakra.schema.protobuf.et_def_pb2 import (
    NodeType as ChakraNodeType,
)
from chakra.src.third_party.utils.protolib import encodeMessage as encode_message


NODE_ID = 0


def get_node(node_name: str, node_type: ChakraNodeType) -> ChakraNode:
    """Generate a new ChakraNode with a unique ID."""
    global NODE_ID
    node = ChakraNode()
    node.id = NODE_ID
    node.name = node_name
    node.type = node_type
    NODE_ID += 1
    return node


def get_comm_type_attr(comm_type: str) -> ChakraAttr:
    """Create a communication type attribute."""
    comm_type_enum_map = {
        'AR': ALL_REDUCE,
        'AG': ALL_GATHER,
        'RS': REDUCE_SCATTER
    }
    comm_type_enum = comm_type_enum_map[comm_type]
    return ChakraAttr(name="comm_type", int64_val=comm_type_enum)

def get_comm_size_descriptor(comm_size: int) -> str:
    if comm_size % 1024 != 0:
        return f"{comm_size}B"
    if comm_size % 1048576 != 0:
        return f"{int(comm_size / 1024)}KB"
    return f"{int(comm_size / 1048576)}MB"


def generate_microbenchmark(num_npus: int, comm_size: int, comm_type: str, num_iters: int) -> None:
    """Generate communication collective nodes."""
    comm_size_desc = get_comm_size_descriptor(comm_size)
    result_dirname = f"genie_micro_{comm_type}_{num_npus}npu_{comm_size_desc}_{num_iters}r"
    if not os.path.exists(result_dirname):
        os.makedirs(result_dirname)

    for npu_id in range(num_npus):
        output_filename = f"{result_dirname}/microbench.{npu_id}.et"
        with open(output_filename, "wb") as et:
            encode_message(et, GlobalMetadata(version="0.0.4"))

            for local_node_id in range(num_iters):
                node = get_node(f"{comm_type}_{local_node_id}", COMM_COLL_NODE)
                node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
                node.attr.extend([get_comm_type_attr(comm_type), ChakraAttr(name="comm_size", int64_val=comm_size)])
                if local_node_id % num_iters != 0:
                    node.data_deps.append(node.id - 1)
                encode_message(et, node)

def main() -> None:
    parser = argparse.ArgumentParser(description="Execution Trace Generator")
    parser.add_argument("--num_npus", type=int, default=4, help="Number of NPUs")
    parser.add_argument(
        "--comm_size", type=int, default=65536, help="Communication size of collectives in bytes" 
    )
    parser.add_argument(
        "--comm_type", type=str, default="AR", help="Comm type. One of 'AR', 'AG', 'RS'. Default: 'AR'."
    )
    parser.add_argument(
        "--num_iters", type=int, default=1, help="Number of back-to-back iterations of this collective"
    )
    args = parser.parse_args()

    if args.comm_type not in ['AR', 'AG', 'RS']:
        print("Comm type must be in 'AR', 'AG', or 'RS'.")
        exit(1)
    generate_microbenchmark(args.num_npus, args.comm_size, args.comm_type, args.num_iters)


if __name__ == "__main__":
    main()
