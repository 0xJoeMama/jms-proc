#include <iostream>
#include <sstream>
#include <sys/wait.h>

#include "except.h"
#include "pool.h"

int main(void) {
  JobPool pool(12);
  try {
    const Proc *last;
    for (int i = 0; i < 12; i++) {
      std::ostringstream ss{};
      ss << i;

      last = &pool.start_job({"echo", "makis", ss.str()});
    }

    auto res = pool.get_by_pid(last->get_pid());
    sleep(2);
    if (res) {
      std::cout << "Status is: " << (*res)->get_status() << std::endl;
    } else {
      throw "failed to find proc";
    }
  } catch (const PosixException &pe) {
    std::cerr << pe.what() << std::endl;
    throw pe;
  } catch (const FullPoolException &e) {
    std::cerr << e.what() << std::endl;
    throw e;
  }
}
