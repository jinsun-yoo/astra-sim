#ifndef NCCL_RENDEZVOUS_HH
#define NCCL_RENDEZVOUS_HH

#include "nccl_net_adapter.hh"
#include <vector>

namespace genie {

// Helper to exchange string metadata using MPI or other rendezvous mechanism.
// The implementation below uses MPI when available; otherwise it leaves
// a TODO for Redis-based exchange.

std::vector<RendezvousMsg> exchange_metadata_via_mpi(const std::string &local_serialized);

} // namespace genie

#endif // NCCL_RENDEZVOUS_HH
