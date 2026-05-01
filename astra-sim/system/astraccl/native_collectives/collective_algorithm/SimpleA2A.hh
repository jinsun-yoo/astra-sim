#ifndef __SIMPLE_A2A_HH__
#define __SIMPLE_A2A_HH__

#include <vector>
#include "astra-sim/system/astraccl/GenieCollective.hh"
// TODO: Not a good idea to scatter macros defining # qps around codebase.
#define A2A_NUM_QPS_PER_RANK 2 
#define NUM_INFLIGHT_CHUNKS_PER_QP 4
#define P2P_STEP_SIZE 131072
#define NUM_RANKS 4

namespace AstraSim{ 
class SimpleA2A: public GenieCollective {
    public: 
        SimpleA2A (int id, uint64_t data_size_bytes, ComType collective_type);
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
        // One for each QP
        int sim_send_cnt[NUM_RANKS][A2A_NUM_QPS_PER_RANK] = {};
        int sim_recv_cnt[NUM_RANKS][A2A_NUM_QPS_PER_RANK] = {};
        int polled_recv_cnt[NUM_RANKS][A2A_NUM_QPS_PER_RANK] = {};
        int polled_send_cnt[NUM_RANKS][A2A_NUM_QPS_PER_RANK] = {};
        bool finished[NUM_RANKS][A2A_NUM_QPS_PER_RANK] = {};
        // We use vector here because num_msgs_per_qp is not determined. I know, we'll have problems since generally num_ranks and num_qps_per_rank is also not fixed, but that's a later problem. 
        std::vector<std::vector<int>> marker; // [NUM_RANKS * A2A_NUM_QPS_PER_RANK][num_msgs_per_qp], tracks send/recv completion per message index.
        int collective_size_mb;
        int num_msgs_per_qp;
        ComType collective_type;
    private:
        static int collective_size_from_env_mb;
        static void get_collective_size_from_env();
        Tick start_ts_nano;
};
}

#endif
