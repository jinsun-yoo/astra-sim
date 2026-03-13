#ifndef NCCL_NET_PLUGIN_V10_HH
#define NCCL_NET_PLUGIN_V10_HH

#include <cstddef>
#include <cstdint>

// Maximum size in bytes of the opaque handle returned by listen().
// Matches NCCL_NET_HANDLE_MAXSIZE in the nccl-net headers.
#define NCCL_NET_HANDLE_MAXSIZE 128

// Minimal representation of the ncclNet_v10 plugin struct where function
// pointers are stored as void* so we can cast at runtime. We avoid depending
// on nccl's headers here.
typedef struct {
  const char* name;
  void* init;
  void* devices;
  void* getProperties;
  void* listen;
  void* connect;
  void* accept;
  void* regMr;
  void* regMrDmaBuf;
  void* deregMr;
  void* isend;
  void* irecv;
  void* iflush;
  void* test;
  void* closeSend;
  void* closeRecv;
  void* closeListen;
  void* getDeviceMr;
  void* irecvConsumed;
  void* makeVDevice;
} ncclNet_v10_t;

#endif // NCCL_NET_PLUGIN_V10_HH
