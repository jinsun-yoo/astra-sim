#ifndef __SIMPLE_RING_HH__
#define __SIMPLE_RING_HH__

#include "astra-sim/system/astraccl/Algorithm.hh"
// TODO: Not a good idea to scatter macros defining # qps around codebase.
#define NUM_QPS 2 
#define NUM_CHUNKS_PER_QP 4 
#define MSG_SIZE_MB 1 
#define NUM_RANKS 4

namespace AstraSim{ 
class SimpleRing: public Algorithm {
    public: 
        SimpleRing (int id);
        virtual void run(EventType event, CallData* data);
        void exit();
        void inject_init_msgs(sim_request& snd_req, sim_request& rcv_req);
        void inject_next_msg(RecvPacketEventHandlerData* data, sim_request& snd_req, sim_request& rcv_req);
        void inject_next_msg_no_ehd(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        void mark_recv_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        void mark_send_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        void inject_next_send(int qp_idx, sim_request& snd_req, sim_request& rcv_req);
        
        int id;
        // One for each QP
        int sim_send_cnt[NUM_QPS] = {0,0};
        int sim_recv_cnt[NUM_QPS] = {0,0};
        int polled_recv_cnt[NUM_QPS] = {0,0};
        int polled_send_cnt[NUM_QPS] = {0,0};
        bool finished[NUM_QPS] = {false, false};
        int marker[NUM_QPS][3072] = {{0}}; // 3072 is an arbitrary number larger than num_msgs_per_qp, which is at most 1152 with current parameters. This tracks whether the send/recv completion has been polled for a given message index, so we know when to inject the next message.
        int send_dst;
        int recv_src;
        int collective_size_mb;
        int num_msgs_per_qp;
};
}

#endif
