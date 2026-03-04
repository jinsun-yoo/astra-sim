#include "nccl_net_loader.hh"
#include "nccl_net_plugin_v10.hh"
#include <dlfcn.h>
#include <iostream>

NcclNetLoader::NcclNetLoader(const std::string &path) : _handle(nullptr), _available(false), _plugin(nullptr) {
    _handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!_handle) {
        std::cerr << "NcclNetLoader: dlopen failed: " << dlerror() << std::endl;
        _available = false;
        return;
    }

    // Try known exported plugin symbol names used by nccl-net plugins.
    const char* candidates[] = {
        "ncclNetPlugin_v10",
        "ncclNetPlugin_v9",
        "ncclNetPlugin_v8",
        "ncclNetPlugin_v7",
        "ncclNetPlugin_v6",
        "ncclNetPlugin_v5",
        "ncclNetPlugin_v4",
        "ncclNetPlugin_v3",
        "ncclNetPlugin_v2",
        nullptr
    };

    const char** p = candidates;
    const char* found = nullptr;
    while (*p) {
        void* sym = dlsym(_handle, *p);
        if (sym) {
            found = *p;
            _plugin = reinterpret_cast<ncclNet_v10_t*>(sym);
            break;
        }
        ++p;
    }

    if (!found) {
        std::cerr << "NcclNetLoader: no ncclNetPlugin_vX symbol found in plugin; plugin may be incompatible" << std::endl;
        dlclose(_handle);
        _handle = nullptr;
        _available = false;
        _plugin = nullptr;
        return;
    }

    std::cerr << "NcclNetLoader: found plugin symbol: " << found << std::endl;
    _available = true;
}

NcclNetLoader::~NcclNetLoader() {
    if (_handle) dlclose(_handle);
}

bool NcclNetLoader::available() const { return _available; }

void* NcclNetLoader::sym(const char* name) const {
    if (!_handle) return nullptr;
    return dlsym(_handle, name);
}

// Return pointer to discovered plugin struct (may be nullptr)
const ncclNet_v10_t* NcclNetLoader::plugin() const {
    return _plugin;
}
