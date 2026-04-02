#include <cassert>
#include <iostream>
#include <sstream>
#include <sys/wait.h>

#include "except.hpp"
#include "pool.hpp"

#define NPROC 200

using pool::JobPool;

int main(void) {
  JobPool pool(NPROC);

  try {
    for (int i = 0; i < NPROC; i++) {
      std::ostringstream ss{};
      ss << i;

      pool.start_job({"echo", "makis", ss.str()});
    }

    pool.wait_all();
  } catch (const psx::PosixException &pe) {
    std::cerr << pe.what() << std::endl;
    throw pe;
  } catch (const FullPoolException &e) {
    std::cerr << e.what() << std::endl;
    throw e;
  }
}
