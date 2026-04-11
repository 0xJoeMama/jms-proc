#include <iostream>

#include "pipe.hpp"

int main(void) {
  psx::Fifo fifo("makis", false);
  if (!fork()) {
    psx::WriteEnd w = fifo.into_write();

    int x = 69;
    w.write_all(x);

    exit(0);
  }

  psx::ReadEnd r = fifo.into_read();
  sleep(5);
  int x;
  r.read_all(x);
  std::cout << "Got " << x << std::endl;
  return 0;
}
