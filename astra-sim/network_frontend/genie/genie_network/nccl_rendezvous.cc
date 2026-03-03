#include "nccl_rendezvous.hh"
#include <iostream>
#ifdef USE_MPI
#include <mpi.h>
#endif

namespace genie {

std::vector<RendezvousMsg> exchange_metadata_via_mpi(const std::string &local_serialized) {
    std::vector<RendezvousMsg> out;
#ifdef USE_MPI
    int world_size = 0;
    int rank = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Gather sizes first
    int local_size = static_cast<int>(local_serialized.size());
    std::vector<int> sizes(world_size);
    MPI_Allgather(&local_size, 1, MPI_INT, sizes.data(), 1, MPI_INT, MPI_COMM_WORLD);

    std::vector<int> displs(world_size);
    int total = 0;
    for (int i = 0; i < world_size; ++i) { displs[i] = total; total += sizes[i]; }
    std::string allbuf;
    allbuf.resize(total);

    MPI_Allgatherv(local_serialized.data(), local_size, MPI_CHAR,
                   &allbuf[0], sizes.data(), displs.data(), MPI_CHAR, MPI_COMM_WORLD);

    // parse the concatenated buffer into messages
    for (int i = 0; i < world_size; ++i) {
        std::string piece = allbuf.substr(displs[i], sizes[i]);
        RendezvousMsg m = RendezvousMsg::deserialize(piece);
        out.push_back(m);
    }
#else
    std::cerr << "exchange_metadata_via_mpi: MPI not enabled; rendezvous not implemented" << std::endl;
#endif
    return out;
}

} // namespace genie
