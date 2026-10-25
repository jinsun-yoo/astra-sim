#ifndef __GENIE_COLLECTIVE_HH__
#define __GENIE_COLLECTIVE_HH__

#include "astra-sim/system/astraccl/Algorithm.hh"

namespace AstraSim {
class GenieCollective: public Algorithm {
    public:
        virtual void mark_send_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) = 0;
        virtual void mark_recv_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) = 0;
        // Routing identity used by the Genie network to deliver completions when several
        // collectives / p2p ops are in flight at once (one per communicator, or per p2p peer+direction).
        virtual int genie_comm_group_id() const = 0;
        // Peer rank of a point-to-point op, -1 for collectives.
        virtual int genie_p2p_peer() const { return -1; }
        virtual bool genie_p2p_is_send() const { return false; }
};
}

#endif
