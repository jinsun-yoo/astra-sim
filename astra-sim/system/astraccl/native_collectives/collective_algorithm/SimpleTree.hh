/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __SIMPLE_TREE_HH__
#define __SIMPLE_TREE_HH__

#include <vector>
#include "astra-sim/system/astraccl/GenieCollective.hh"

// TODO: Not a good idea to scatter macros defining # qps around codebase.
#define TREE_MAX_NUM_QPS 32
#define TREE_NUM_INFLIGHT_CHUNKS_PER_QP 4
#define TREE_MSG_SIZE_MB 1
#define TREE_NUM_RANKS 4
#define TREE_MAX_PEERS 2  // max parent or child peers per rank

namespace AstraSim {

// SimpleTree implements a Double Binary Tree AllReduce for exactly 4 ranks.
//
// Tree A: Leaves={0,2}, Intermediate=1, Root=3
//   Reduce:    0->1, 2->1, 1->3
//   Broadcast: 3->1, 1->0, 1->2
//
// Tree B: Leaves={1,3}, Intermediate=2, Root=0
//   Reduce:    1->2, 3->2, 2->0
//   Broadcast: 0->2, 2->1, 2->3
//
// Data is split evenly between the two trees (data_size/2 per tree).
// Both trees operate simultaneously in each phase.
//
// "parent_peers": ranks this node SENDs to during reduce / RECVs from during broadcast.
// "child_peers":  ranks this node RECVs from during reduce / SENDs to during broadcast.
//
// Counters sim_send_cnt[i][j], sim_recv_cnt[i][j], etc. are 2D to account for
// upstream (parent) and downstream (child) peers:
//   REDUCE phase:    sim_send_cnt[i] -> sends to parent_peers[i]
//                    sim_recv_cnt[i] -> recvs from child_peers[i]
//   BROADCAST phase: sim_send_cnt[i] -> sends to child_peers[i]
//                    sim_recv_cnt[i] -> recvs from parent_peers[i]
// Counters are reset when transitioning between phases.
class SimpleTree : public GenieCollective {
  public:
    enum class Phase { REDUCE, BROADCAST };

    // id: this rank's ID (must be in [0, TREE_NUM_RANKS)).
    // data_size_bytes: total AllReduce data size across both trees.
    // collective_type: must be ComType::All_Reduce.
    // comm_group_id: communication group identifier.
    SimpleTree(int id,
               uint64_t data_size_bytes,
               ComType collective_type,
               int comm_group_id);

    virtual void run(EventType event, CallData* data);
    void exit();

    // Called on StreamInit: records start timestamp and posts the initial
    // wave of reduce-phase sends and recvs.
    void inject_init_msgs(sim_request& snd_req, sim_request& rcv_req);

    // Low-level helpers: post a single send to peer_rank / recv from peer_rank
    // on the given QP, updating sim_send_cnt[peer_idx][qp_idx] /
    // sim_recv_cnt[peer_idx][qp_idx].
    void post_send(int peer_rank, int peer_idx, int qp_idx);
    void post_recv(int peer_rank, int peer_idx, int qp_idx);

    // GenieCollective interface: called by the genie network on completion events.
    // peer rank is recovered from snd_req.dstRank (same field for both send and
    // recv completions as set by the genie network layer).
    void mark_recv_complete(int qp_idx,
                            sim_request& snd_req,
                            sim_request& rcv_req) override;
    void mark_send_complete(int qp_idx,
                            sim_request& snd_req,
                            sim_request& rcv_req) override;

    // Transitions to broadcast phase: resets per-phase counters and posts the
    // initial wave of broadcast-phase sends/recvs.
    void start_broadcast();

    // Called after every completion event to detect phase/algorithm completion.
    void check_reduce_complete();
    void check_bcast_complete();

    void record_stats();

    int id;
    int comm_group_id;
    Phase current_phase;

    // -----------------------------------------------------------------------
    // Topology (hardcoded for TREE_NUM_RANKS = 4).
    // -----------------------------------------------------------------------

    // Ranks this node sends to during reduce (= recvs from during broadcast).
    int parent_peers[TREE_MAX_PEERS];
    int num_parent_peers;

    // Ranks this node recvs from during reduce (= sends to during broadcast).
    int child_peers[TREE_MAX_PEERS];
    int num_child_peers;

    // parent_send_needs_children[i] = true means the send to parent_peers[i]
    // during reduce is CONDITIONAL: it may only be posted once all child_peers
    // have delivered the corresponding message chunk (intermediate-node role).
    bool parent_send_needs_children[TREE_MAX_PEERS];

    // Index into parent_peers[] of the single conditional parent send during
    // reduce, or -1 if no conditional send exists for this rank.
    int reduce_parent_conditional_idx;

    // true for root-role ranks: child sends in broadcast are posted immediately
    // at phase start (no dependency on any incoming recv).
    bool bcast_send_immediate;

    // Index into parent_peers[] whose recv completion in broadcast triggers
    // sends to ALL child_peers for that message, or -1 if not applicable.
    int bcast_trigger_parent_idx;

    // -----------------------------------------------------------------------
    // Per-peer, per-QP counters (reset between phases).
    // sim_send_cnt / sim_recv_cnt: number of messages *posted*.
    // polled_send_cnt / polled_recv_cnt: number of completions *received*.
    // -----------------------------------------------------------------------
    int sim_send_cnt[TREE_MAX_PEERS][TREE_MAX_NUM_QPS];
    int sim_recv_cnt[TREE_MAX_PEERS][TREE_MAX_NUM_QPS];
    int polled_send_cnt[TREE_MAX_PEERS][TREE_MAX_NUM_QPS];
    int polled_recv_cnt[TREE_MAX_PEERS][TREE_MAX_NUM_QPS];

    // Rank-to-peer-index lookups (-1 if the rank is not in that peer list).
    int parent_rank_to_idx[TREE_NUM_RANKS];
    int child_rank_to_idx[TREE_NUM_RANKS];

    // child_reduce_marker[qp][msg] counts how many child_peers have completed
    // recv for that (qp, msg) slot during reduce, used to gate the conditional
    // parent send (only meaningful when reduce_parent_conditional_idx >= 0).
    std::vector<std::vector<int>> child_reduce_marker;

    // Flow control for conditional reduce sends to avoid RingTrain overflow.
    // cond_send_inflight[qp]: conditional sends currently posted but not acked.
    // cond_send_pending[qp]:  marker-fired sends waiting for an in-flight slot.
    // Both reset to 0 at init (reduce only; broadcast doesn't use this path).
    int cond_send_inflight[TREE_MAX_NUM_QPS];
    int cond_send_pending[TREE_MAX_NUM_QPS];

    // Flow control for broadcast triggered child sends (per child, per QP).
    // Only used when bcast_send_immediate=false (intermediate ranks 1 and 2).
    // Reset in start_broadcast().
    int bcast_trig_inflight[TREE_MAX_PEERS][TREE_MAX_NUM_QPS];
    int bcast_trig_pending[TREE_MAX_PEERS][TREE_MAX_NUM_QPS];

    int collective_size_mb;
    int num_msgs_per_qp;
    ComType collective_type;

  private:
    static int collective_size_from_env_mb;
    static void get_collective_size_from_env();
    static int num_qps;
    static void get_num_qps_from_env();
    Tick start_ts_nano;
};

}  // namespace AstraSim

#endif /* __SIMPLE_TREE_HH__ */
