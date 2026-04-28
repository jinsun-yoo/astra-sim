#ifndef __SIMPLE_RING_HH__
#define __SIMPLE_RING_HH__

#include <vector>
#include "astra-sim/system/astraccl/GenieCollective.hh"
// TODO: Not a good idea to scatter macros defining # qps around codebase.
#define NUM_QPS 2 
#define NUM_INFLIGHT_CHUNKS_PER_QP 4
#define MSG_SIZE_MB 1 
#define NUM_RANKS 4

namespace AstraSim{ 
class SimpleRing: public GenieCollective {
    public: 
        SimpleRing (int id, uint64_t data_size_bytes, ComType collective_type);
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
        int sim_send_cnt[NUM_QPS] = {};
        int sim_recv_cnt[NUM_QPS] = {};
        int polled_recv_cnt[NUM_QPS] = {};
        int polled_send_cnt[NUM_QPS] = {};
        bool finished[NUM_QPS] = {};
        std::vector<std::vector<int>> marker; // [NUM_QPS][num_msgs_per_qp], tracks send/recv completion per message index.
        int send_dst;
        int recv_src;
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
