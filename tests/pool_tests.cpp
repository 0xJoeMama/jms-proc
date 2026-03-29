#include <cassert>
#include <iostream>
#include <sstream>
#include <sys/wait.h>

#include "except.hpp"
#include "pool.hpp"

#define NPROC 200

using pool::Job;
using pool::JobPool;
using pool::JobStatus;

int main(void) {
  JobPool pool(NPROC);

  try {
    const Job *last;
    for (int i = 0; i < NPROC; i++) {
      std::ostringstream ss{};
      ss << i;

      last = &pool.start_job({"echo", "makis", ss.str()});
    }

    auto res = pool.get_by_pid(last->get_pid());
    assert(res && res.value()->get_status() == JobStatus::Active);

    pool.wait_all();

    res = pool.get_by_pid(last->get_pid());
    assert(res && res.value()->get_status() == JobStatus::Finished);
  } catch (const PosixException &pe) {
    std::cerr << pe.what() << std::endl;
    throw pe;
  } catch (const FullPoolException &e) {
    std::cerr << e.what() << std::endl;
    throw e;
  }
}
