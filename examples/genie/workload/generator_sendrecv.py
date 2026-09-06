import argparse

from chakra.schema.protobuf.et_def_pb2 import (
    COMM_SEND_NODE,
    COMM_RECV_NODE,
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


def append_comm_attrs(
    node: ChakraNode, comm_size: int, comm_src: int, comm_dst: int
) -> None:
    node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
    node.attr.extend([
        ChakraAttr(name="comm_type", int64_val=node.type),
        ChakraAttr(name="comm_size", int64_val=comm_size),
        ChakraAttr(name="comm_src", int32_val=comm_src),
        ChakraAttr(name="comm_dst", int32_val=comm_dst),
    ])


def generate_unidir(output_basename: str, comm_size_mb: int, num_iters: int) -> None:
    """Generate traces where rank 0 sends and rank 1 receives."""
    comm_size = comm_size_mb * 1024 * 1024
    rank_specs = [(0, COMM_SEND_NODE), (1, COMM_RECV_NODE)]
    comm_src = 0
    comm_dst = 1
    for (npu_id, comm_type) in rank_specs:
        output_filename = f"{output_basename}.{npu_id}.et"
        parent_node_id = -1
        with open(output_filename, "wb") as et:
            encode_message(et, GlobalMetadata(version="0.0.4"))
            for iter_idx in range(num_iters):
                node = get_node(
                    f"sendrecv_0_1_{comm_size}_bytes_{iter_idx}_{comm_type}",
                    comm_type,
                )
                append_comm_attrs(node, comm_size, comm_src, comm_dst)
                if parent_node_id != -1:
                    node.data_deps.append(parent_node_id)
                parent_node_id = node.id
                encode_message(et, node)


def generate_bidir(output_basename: str, comm_size_mb: int, num_iters: int) -> None:
    """Generate traces where both ranks send to and receive from each other."""
    comm_size = comm_size_mb * 1024 * 1024
    for npu_id in range(2):
        output_filename = f"{output_basename}.{npu_id}.et"
        previous_node_ids = []
        with open(output_filename, "wb") as et:
            encode_message(et, GlobalMetadata(version="0.0.4"))
            for iter_idx in range(num_iters):
                current_node_ids = []
                for node_type, comm_src, comm_dst in (
                    (COMM_SEND_NODE, npu_id, npu_id ^ 1),
                    (COMM_RECV_NODE, npu_id ^ 1, npu_id),
                ):
                    node = get_node(
                        f"sendrecv_{comm_src}_{comm_dst}_{comm_size}_bytes_"
                        f"{iter_idx}_{node_type}",
                        node_type,
                    )
                    append_comm_attrs(node, comm_size, comm_src, comm_dst)
                    node.data_deps.extend(previous_node_ids)
                    current_node_ids.append(node.id)
                    encode_message(et, node)
                previous_node_ids = current_node_ids


def main() -> None:
    parser = argparse.ArgumentParser(description="Sendrecv trace generator")
    parser.add_argument(
        "--direction", choices=("unidir", "bidir"), required=True,
        help="Communication direction to generate",
    )
    parser.add_argument(
        "--comm_size_mb", type=int, default=1024,
        help="Communication size in MiB",
    )
    parser.add_argument("--num_iters", type=int, default=8, help="Number of iterations")
    args = parser.parse_args()

    generator = generate_unidir if args.direction == "unidir" else generate_bidir
    direction_str = "UNIDIRECTIONAL" if args.direction == "unidir" else "BIDIRECTIONAL"
    output_basename = f"SENDRECV_{direction_str}_{args.comm_size_mb}"
    generator(output_basename, args.comm_size_mb, args.num_iters)


if __name__ == "__main__":
    main()
