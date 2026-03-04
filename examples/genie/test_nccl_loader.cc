#include "nccl_net_loader.hh"
#include <iostream>
int main() {
  NcclNetLoader l("/mnt/aiml/jinsun/nccl/ext-net/example/libnccl-net.so");
  std::cout << \"available=\" << l.available() << std::endl;
  return 0;
}
