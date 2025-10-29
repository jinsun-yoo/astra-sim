from chakra.schema.protobuf.et_def_pb2 import *

from chakra.schema.protobuf.et_def_pb2 import (
    AttributeProto as ChakraAttr,
    Node as ChakraNode,
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

comm_size = 1_048_576
num_npus = 8
num_nodes = 10
# Node 0: AG. No PG and no node-specific. Use custom global
# Node 1: AG. No PG and node-specific. Use the node-specific custom impl
# Node 2: AG. Global PG and no node-specific. Use custom global
# Node 3: AG. Global PG and node-specific. Use the node-specific custom impl
# Node 4: AG. PG split into two (0~3, 4~7) and node-specific. Use node-specific custom impl
# Node 5: RS. No PG and no node-specific. Use Native
# Node 6: RS. No PG and node-specific. Use the node-specific custom impl
# Node 7: RS. Global PG and no node-specific. Use Native
# Node 8: RS. Global PG and node-specific. Use the node-specific custom impl
# Node 9: RS. PG split into two (0~3, 4~7) and node-specific. Use node-specific custom impl

def get_pg_and_append(node: ChakraNode, npu_id: int):
    """Generate a process group name based on NPU ID and node ID."""
    if node.id % 5 == 0 or node.id % 5 == 1:
        # For local Chakra nodes 0, 1: do not add any pg_name attribute
        return
    if node.id % 5 == 2 or node.id % 5 == 3:
        # For local Chakra nodes 2, 3: use global pg "0" which includes all 8 ranks.
        pg_id = "0"
    if node.id % 5 == 4:
        # For local Chakra nodes 4: Split into two pgs. Rank 0~3 use pg "1", Rank 4~7 use pg "2".
        if npu_id < (num_npus / 2):
            pg_id = "1"
        else:
            pg_id = "2"
    node.attr.append(ChakraAttr(name="pg_name", string_val=pg_id))

def get_op_and_append(node: ChakraNode, npu_id: int):
    """Generate an operator name based on NPU ID and node ID."""
    if node.id % 10 < 5:
        comm_type = ALL_GATHER
    else:
        comm_type = REDUCE_SCATTER
    node.attr.append(ChakraAttr(name="comm_type", int64_val=comm_type))
    return comm_type


for npu_id in range(num_npus):
    output_filename = f"pernode_validate.{npu_id}.et"
    with open(output_filename, "wb") as et:
        encode_message(et, GlobalMetadata(version="0.0.4"))

        for _ in range(num_nodes):
            node = get_node("PLACEHOLDER", COMM_COLL_NODE)
            node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
            node.attr.append(ChakraAttr(name="comm_size", int64_val=comm_size))
            get_pg_and_append(node, npu_id)
            comm_type = get_op_and_append(node, npu_id)
            if comm_type == ALL_GATHER:
                node.name = "ALL_GATHER"
            else:
                node.name = "REDUCE_SCATTER"
            encode_message(et, node)

