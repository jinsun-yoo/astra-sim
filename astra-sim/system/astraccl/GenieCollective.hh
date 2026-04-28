#ifndef __GENIE_COLLECTIVE_HH__
#define __GENIE_COLLECTIVE_HH__

#include "astra-sim/system/astraccl/Algorithm.hh"

namespace AstraSim {
class GenieCollective: public Algorithm {
    public:
        virtual void mark_send_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) = 0;
        virtual void mark_recv_complete(int qp_idx, sim_request& snd_req, sim_request& rcv_req) = 0;
};
}

#endif