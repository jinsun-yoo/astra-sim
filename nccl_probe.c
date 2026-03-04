#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "./astra-sim/network_frontend/genie/genie_network/nccl_net_plugin_v10.hh"

static void stub_logger(int level, unsigned long flags,
                        const char* file, int line, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}

int main(int argc, char** argv) {
  const char* path = "/mnt/aiml/nccl-rdma-sharp-plugins/src/.libs/libnccl-net.so";
  void* h = dlopen(path, RTLD_NOW);
  if (!h) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }
  void* sym = NULL;
  for (int v = 10; v >= 2; --v) {
    char name[32]; snprintf(name, sizeof(name), "ncclNetPlugin_v%d", v);
    sym = dlsym(h, name);
    if (sym) { printf("found symbol: %s\n", name); break; }
  }
  if (!sym) { fprintf(stderr, "no plugin symbol found\n"); dlclose(h); return 2; }
  ncclNet_v10_t* plugin = (ncclNet_v10_t*)sym;
  printf("plugin->name = %s\n", plugin->name ? plugin->name : "(null)");
  if (plugin->init) {
    int (*init_fn)(void*,void*) = (int(*)(void*,void*))plugin->init;
    int r = init_fn((void*)stub_logger, NULL);
    printf("init returned %d\n", r);
  } else printf("init missing\n");
  if (plugin->devices) {
    int (*dev_fn)(int*) = (int(*)(int*))plugin->devices;
    int n = -1; dev_fn(&n);
    printf("devices() -> %d\n", n);
  } else printf("devices missing\n");
  if (plugin->getProperties) {
    int (*prop_fn)(int, void*) = (int(*)(int,void*))plugin->getProperties;
    char buf[256]; memset(buf,0,sizeof(buf));
    int r = prop_fn(0, buf);
    printf("getProperties(0) returned %d name='%s'\n", r, buf);
  } else printf("getProperties missing\n");
  dlclose(h);
  return 0;
}
