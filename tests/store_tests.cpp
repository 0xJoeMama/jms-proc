#include "except.hpp"
#include "file_store.hpp"
#include "pipe.hpp"
#include <iostream>

int main(void) {
  try {
    store::FileStore store("makis");
    psx::Fifo fifo = store.create_fifo("jms_in");
  } catch (const psx::PosixException &pe) {
    std::cerr << pe.what() << std::endl;
    return 1;
  }

  return 0;
}
