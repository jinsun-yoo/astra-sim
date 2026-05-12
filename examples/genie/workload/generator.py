import argparse

from chakra.schema.protobuf.et_def_pb2 import (
    ALL_GATHER,
    ALL_TO_ALL,
    ALL_REDUCE,
    REDUCE_SCATTER,
    COMM_COLL_NODE,
    COMP_NODE,
    GlobalMetadata,
    CollectiveCommType,
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

def multiple_coll_node_allgather (num_npus: int, comm_size_mb: int, comm_type_str: str, num_iters: int) -> None:
    """Generate multiple allreduce communication collective node."""
    comm_type_str_caps = comm_type_str.upper()
    match comm_type_str_caps:
        case "ALL_GATHER":
            comm_type = ALL_GATHER
        case "ALL_REDUCE":
            comm_type = ALL_REDUCE
        case "REDUCE_SCATTER":
            comm_type = REDUCE_SCATTER
        case "ALLTOALL":
            comm_type = ALL_TO_ALL
        case _:
            raise ValueError(f"Unsupported collective type: {comm_type_str}")
    comm_size = comm_size_mb * 1024 * 1024
    for npu_id in range (num_npus):
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

def multiple_coll_node_interval (num_npus: int, comm_size_mb: int, comm_type_str: str, num_iters: int) -> None:
    """Generate multiple allreduce communication collective node."""
    comm_type_str_caps = comm_type_str.upper()
    match comm_type_str_caps:
        case "ALL_GATHER":
            comm_type = ALL_GATHER
        case "ALL_REDUCE":
            comm_type = ALL_REDUCE
        case "REDUCE_SCATTER":
            comm_type = REDUCE_SCATTER
        case "ALLTOALL":
            comm_type = ALL_TO_ALL
        case _:
            raise ValueError(f"Unsupported collective type: {comm_type_str}")
    comm_size = comm_size_mb * 1024 * 1024
    runtime = 100000
    for npu_id in range (num_npus):
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
    parser.add_argument("--num_npus", type=int, default=64, help="Number of NPUs")
    parser.add_argument(
        "--default_comm_size_mb", type=int, default=1024, help="Default communication size of communication nodes"
    )
    parser.add_argument("--num_iters", type=int, default=30, help="Number of iterations for communication nodes")
    args = parser.parse_args()
    multiple_coll_node_allgather(args.num_npus, args.default_comm_size_mb, "ALLTOALL", args.num_iters)
    # multiple_coll_node_interval(args.num_npus, args.default_comm_size_mb, "ALL_GATHER", args.num_iters)
if __name__ == "__main__":
    main()
