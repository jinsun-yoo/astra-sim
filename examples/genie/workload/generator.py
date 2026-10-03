import argparse
import os

from chakra.schema.protobuf.et_def_pb2 import (
    ALL_GATHER,
    ALL_TO_ALL,
    ALL_REDUCE,
    REDUCE_SCATTER,
    COMM_COLL_NODE,
    COMP_NODE,
    GlobalMetadata,
)
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


def get_comm_type_attr(comm_type: int) -> ChakraAttr:
    """Create a communication type attribute."""
    return ChakraAttr(name="comm_type", int64_val=comm_type)


# Accept both the canonical enum-style name (ALL_TO_ALL) and the common
# alias without underscores (ALLTOALL) so callers don't need to know which
# spelling the Chakra protobuf enum uses internally.
_COMM_TYPE_BY_NAME = {
    "ALL_GATHER": ALL_GATHER,
    "ALL_REDUCE": ALL_REDUCE,
    "REDUCE_SCATTER": REDUCE_SCATTER,
    "ALL_TO_ALL": ALL_TO_ALL,
    "ALLTOALL": ALL_TO_ALL,
}


def resolve_comm_type(comm_type_str: str) -> int:
    """Resolve a collective type name (case-insensitive) to its Chakra enum value."""
    comm_type_str_caps = comm_type_str.upper()
    try:
        return _COMM_TYPE_BY_NAME[comm_type_str_caps]
    except KeyError:
        raise ValueError(f"Unsupported collective type: {comm_type_str}") from None


def generate_coll_node_trace(num_npus: int, comm_size_mb: int, comm_type_str: str, num_iters: int) -> None:
    """Generate a repeated collective-communication-node trace for each NPU."""
    comm_type_str_caps = comm_type_str.upper()
    comm_type = resolve_comm_type(comm_type_str_caps)
    comm_size = comm_size_mb * 1024 * 1024
    for npu_id in range(num_npus):
        output_filename = f"{comm_type_str_caps}_{comm_size_mb}.{npu_id}.et"
        parent_node_id = -1
        with open(output_filename, "wb") as et:
            encode_message(et, GlobalMetadata(version="0.0.4"))
            for node_id in range(num_iters):
                node = get_node(f"{comm_type_str_caps}_{node_id}", COMM_COLL_NODE)
                node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
                node.attr.extend([get_comm_type_attr(comm_type), ChakraAttr(name="comm_size", int64_val=comm_size)])
                if parent_node_id != -1:
                    node.data_deps.append(parent_node_id)
                parent_node_id = node.id
                encode_message(et, node)


def generate_coll_node_interval_trace(num_npus: int, comm_size_mb: int, comm_type_str: str, num_iters: int) -> None:
    """Generate a collective-communication-node trace with a compute interval between iterations."""
    comm_type_str_caps = comm_type_str.upper()
    comm_type = resolve_comm_type(comm_type_str_caps)
    comm_size = comm_size_mb * 1024 * 1024
    runtime = 100000
    for npu_id in range(num_npus):
        output_filename = f"{comm_type_str_caps}_{comm_size_mb}_intervals.{npu_id}.et"
        parent_node_id = -1
        with open(output_filename, "wb") as et:
            encode_message(et, GlobalMetadata(version="0.0.4"))
            for node_id in range(num_iters):
                node = get_node(f"{comm_type_str_caps}_{node_id}", COMM_COLL_NODE)
                node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
                node.attr.extend([get_comm_type_attr(comm_type), ChakraAttr(name="comm_size", int64_val=comm_size)])
                if parent_node_id != -1:
                    node.data_deps.append(parent_node_id)
                parent_node_id = node.id
                encode_message(et, node)

                node = get_node(f"Interval_{node_id}", COMP_NODE)
                node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=True))
                node.duration_micros = runtime
                node.data_deps.append(parent_node_id)
                parent_node_id = node.id
                encode_message(et, node)


def main() -> None:
    parser = argparse.ArgumentParser(description="Execution Trace Generator")
    parser.add_argument("--num_npus", type=int, default=8, help="Number of NPUs")
    parser.add_argument("--num_iters", type=int, default=30, help="Number of iterations for communication nodes")
    parser.add_argument(
        "--comm_type",
        type=str,
        nargs="+",
        default=["ALL_REDUCE"],
        choices=sorted(_COMM_TYPE_BY_NAME.keys()),
        help="One or more collective communication types to generate traces for",
    )
    parser.add_argument(
        "--output_dir",
        type=str,
        default=".",
        help="Directory to write the generated .et trace files into",
    )
    parser.add_argument(
        "--with_interval",
        action="store_true",
        help="Insert a compute interval node between successive collective nodes",
    )
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    prev_cwd = os.getcwd()
    os.chdir(args.output_dir)
    try:
        for comm_type in args.comm_type:
            if args.with_interval:
                generate_coll_node_interval_trace(args.num_npus, args.default_comm_size_mb, comm_type, args.num_iters)
            else:
                generate_coll_node_trace(args.num_npus, args.default_comm_size_mb, comm_type, args.num_iters)
    finally:
        os.chdir(prev_cwd)


if __name__ == "__main__":
    main()
