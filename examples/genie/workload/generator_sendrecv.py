import argparse

from chakra.schema.protobuf.et_def_pb2 import (
    COMM_SEND_NODE,
    COMM_RECV_NODE,
    COMM_COLL_NODE,
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

def sendrecv_unidirectional (comm_size_mb: int, num_iters: int) -> None:
    """Generate multiple allreduce communication collective node."""
    comm_type_str = "SENDRECV_UNIDIRECTIONAL"
    comm_size = comm_size_mb * 1024 * 1024
    for npu_id in range(2):
        output_filename = f"{comm_type_str}_{comm_size_mb}.{npu_id}.et"
        parent_node_id = -1
        with open(output_filename, "wb") as et:
            encode_message(et, GlobalMetadata(version="0.0.4"))
            for node_id in range(num_iters):
                comm_type = COMM_SEND_NODE if npu_id == 0 else COMM_RECV_NODE
                node = get_node(f"{comm_type_str}_{node_id}", comm_type)
                node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
                node.attr.extend([get_comm_type_attr(comm_type), ChakraAttr(name="comm_size", int64_val=comm_size)])
                node.attr.extend([ChakraAttr(name="comm_src", int32_val=0), ChakraAttr(name="comm_dst", int32_val=1)])
                if parent_node_id != -1:
                    node.data_deps.append(parent_node_id)
                parent_node_id = node.id
                encode_message(et, node)

def sendrecv_bidirectional (comm_size_mb: int, num_iters: int) -> None:
    """Generate multiple allreduce communication collective node."""
    comm_type_str = "SENDRECV_BIDIRECTIONAL"
    comm_size = comm_size_mb * 1024 * 1024
    for npu_id in range(2):
        output_filename = f"{comm_type_str}_{comm_size_mb}.{npu_id}.et"
        with open(output_filename, "wb") as et:
            node_id = 0
            encode_message(et, GlobalMetadata(version="0.0.4"))
            for iter_idx in range(num_iters):
                comm_type = COMM_SEND_NODE
                node = get_node(f"{comm_type_str}_{node_id}", comm_type)
                node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
                node.attr.extend([get_comm_type_attr(comm_type), ChakraAttr(name="comm_size", int64_val=comm_size)])
                node.attr.extend([ChakraAttr(name="comm_src", int32_val=npu_id), ChakraAttr(name="comm_dst", int32_val=npu_id ^ 1)])
                if iter_idx > 0:
                    node.data_deps.append((iter_idx -1) * 2 )
                    node.data_deps.append((iter_idx -1) * 2 + 1 )
                encode_message(et, node)
                node_id += 1

                comm_type = COMM_RECV_NODE
                node = get_node(f"{comm_type_str}_{node_id}", comm_type)
                node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
                node.attr.extend([get_comm_type_attr(comm_type), ChakraAttr(name="comm_size", int64_val=comm_size)])
                node.attr.extend([ChakraAttr(name="comm_src", int32_val=npu_id ^ 1), ChakraAttr(name="comm_dst", int32_val=npu_id)])
                if iter_idx > 0:
                    node.data_deps.append((iter_idx -1) * 2 )
                    node.data_deps.append((iter_idx -1) * 2 + 1 )
                encode_message(et, node)
                node_id += 1


def main() -> None:
    parser = argparse.ArgumentParser(description="Execution Trace Generator")
    parser.add_argument("--num_npus", type=int, default=64, help="Number of NPUs")
    parser.add_argument(
        "--default_comm_size_mb", type=int, default=1024, help="Default communication size of communication nodes"
    )
    parser.add_argument("--num_iters", type=int, default=30, help="Number of iterations for communication nodes")
    args = parser.parse_args()
    sendrecv_unidirectional(args.default_comm_size_mb, args.num_iters)
    sendrecv_bidirectional(args.default_comm_size_mb, args.num_iters)
if __name__ == "__main__":
    main()
