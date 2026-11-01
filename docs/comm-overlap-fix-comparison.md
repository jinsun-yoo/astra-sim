# Comm-overlap fixes: `genie_dev_scratch` vs `comm-overlap-fix1` vs `comm-overlap-fix2`

Both fix branches address the same problem: two collectives or send/recvs from
different comm groups in flight at once collide on the single
`genie_collective_ptr`. They solve it in opposite ways.

- `comm-overlap-fix1` (this branch, formerly `champollion-fix-concurrent`): keeps
  the serial per-rank scale-out queue already in `genie_dev_scratch` and adds
  node-id issue ordering (to avoid cross-rank deadlock) and per-member-set gloo
  pair channels (to fix `pair.cc:382`). Small change, no overlap across
  communicators.
- `comm-overlap-fix2` (formerly `genie_comm_sequencer`): replaces the
  serialization. Completions are routed per comm group and per p2p
  (peer, direction), and a comm sequencer runs one op at a time per communicator
  while different communicators run concurrently. Larger, invasive change.

| | `genie_dev_scratch` (`327829d`) | `comm-overlap-fix1` | `comm-overlap-fix2` |
|---|---|---|---|
| Base | `327829d` | `327829d` | `6840dd7` (earlier in history) |
| Single `genie_collective_ptr` collision | Yes, by serializing: `GENIE_SCALEOUT_QUEUE` puts all scale-out comm of a rank on one queue. `genie_collective_ptr` is still a single pointer. | Same, inherited from `genie_dev_scratch`. | Replaced by per-comm-group and per-p2p routing; no serialization (`GENIE_SCALEOUT_QUEUE` absent). |
| Cross-rank issue-order deadlock (`GENIE_SCALEOUT_ORDER`) | No | Yes (`GENIE_SCALEOUT_ORDER=id` default, `=ready` for old order) | Not addressed; ordering is per communicator only. |
| Gloo pair collision (`pair.cc:382`) | No | Yes, disjoint pairs per member set | Yes, inside the concurrent-communicators commit |
| Comm sequencer | No | No | Yes (`GENIE_NO_COMM_SEQUENCER=1` disables) |
| Concurrency across communicators | None | None | Yes |
| Size vs base | n/a | ~134 lines, 8 files | ~443 lines, 18 files |
| Extras | | | Chrome-trace `WORKLOAD_SEQREL` marker |

`comm-overlap-fix1` is `genie_dev_scratch` plus the ordering fix and the pair
fix. `comm-overlap-fix2` replaces the serialization with real concurrency.

## Trade-offs

- `comm-overlap-fix1` is small and safe and serial, which is arguably closer to
  a single NCCL stream. It loses FSDP2 all-gather/reduce-scatter and HSDP
  overlap.
- `comm-overlap-fix2` models overlap, but changes the network, `Sys`,
  `GenieCollective` and all `Simple*` algorithms. Its sequencer orders by node-id
  per communicator only, so it gives no cross-communicator order.
- Both change the same paths in `Workload.cc` and `genie_network.cc`. Reconcile
  them rather than stacking them.

This comparison is based on commit messages, diff stats and grepping each tree
for the mechanisms; neither branch was run for it.
