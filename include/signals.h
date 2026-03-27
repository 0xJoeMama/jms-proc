#include <initializer_list>
#include <iostream>
#include <signal.h>
#include <sys/types.h>

#include "except.h"

class SignalLock {
  sigset_t old_set;

public:
  SignalLock(std::initializer_list<int> signals) {
    sigset_t blocked;

    sigemptyset(&blocked);
    for (auto i : signals) {
      if (sigaddset(&blocked, i) < 0)
        throw PosixException();
    }

    if (sigprocmask(SIG_BLOCK, &blocked, &old_set) < 0)
      throw PosixException();
  }

  ~SignalLock() {
    if (sigprocmask(SIG_SETMASK, &old_set, nullptr) < 0)
      std::cerr
          << "ERROR: failed to unblock blocked signals. Here be dragons..."
          << std::endl;
  }
};
