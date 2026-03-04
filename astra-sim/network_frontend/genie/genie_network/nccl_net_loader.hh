#ifndef NCCL_NET_LOADER_HH
#define NCCL_NET_LOADER_HH

#include <string>
#include "nccl_net_plugin_v10.hh"

// Minimal runtime loader for an nccl-net plugin using dlopen/dlsym.
// Provides availability check and symbol lookup. Does not assume compile-time
// headers for nccl-net so it is safe if the plugin is absent.
class NcclNetLoader {
public:
    explicit NcclNetLoader(const std::string &path = "libnccl-net.so");
    ~NcclNetLoader();

    // True if plugin was successfully opened and at least one expected symbol
    // was found.
    bool available() const;

    // Lookup a symbol; returns nullptr if not found or plugin not loaded.
    void* sym(const char* name) const;

    // Return pointer to plugin struct if available
    const ncclNet_v10_t* plugin() const;

private:
    void* _handle;
    bool _available;
    ncclNet_v10_t* _plugin;
};

#endif // NCCL_NET_LOADER_HH
