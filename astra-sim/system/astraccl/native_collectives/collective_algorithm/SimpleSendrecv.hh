#ifndef __SIMPLE_SENDRECV_HH__
#define __SIMPLE_SENDRECV_HH__

#include <vector>
#include "astra-sim/system/astraccl/GenieCollective.hh"
// TODO: Not a good idea to scatter macros defining # qps around codebase.
#define A2A_NUM_QPS_PER_RANK 2 
#define NUM_INFLIGHT_CHUNKS_PER_QP 8
#define P2P_STEP_SIZE 131072
// #define TRACE_SimpleSendrecv 1

namespace AstraSim{ 
class SimpleSendrecv: public GenieCollective {
    public: 
        SimpleSendrecv (int id, int peer_rank, bool is_send, uint64_t data_size_bytes, Sys *sys, WorkloadLayerHandlerData* wlhd = nullptr, int comm_group_id = 0);
        virtual void run(EventType event, CallData* data);
        void exit();
        void inject_init_msgs(sim_request& snd_req, sim_request& rcv_req);
        void inject_next_msg(RecvPacketEventHandlerData* data, sim_request& snd_req, sim_request& rcv_req);
        void inject_next_msg_no_ehd(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        void mark_recv_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        void mark_send_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        void inject_next_send(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        void record_stats();
        
        int id;
        int peer_rank;
        bool is_send;
        int comm_group_id;
        Sys *sys;
        WorkloadLayerHandlerData* wlhd;
        // One for each QP
        int sim_send_cnt[A2A_NUM_QPS_PER_RANK] = {};
        int sim_recv_cnt[A2A_NUM_QPS_PER_RANK] = {};
        int polled_recv_cnt[A2A_NUM_QPS_PER_RANK] = {};
        int polled_send_cnt[A2A_NUM_QPS_PER_RANK] = {};
        bool finished[A2A_NUM_QPS_PER_RANK] = {};
        // We use vector here because num_msgs_per_qp is not determined. I know, we'll have problems since generally num_ranks and num_qps_per_rank is also not fixed, but that's a later problem. 
        std::vector<std::vector<int>> marker; // [A2A_NUM_QPS_PER_RANK][num_msgs_per_qp], tracks send/recv completion per message index.
        int collective_size_mb;
        int num_msgs_per_qp;
    private:
        static int collective_size_from_env_mb;
        static void get_collective_size_from_env();
        Tick start_ts_nano;
};
}

#endif
