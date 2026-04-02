#include <fcntl.h>
#include <iostream>
#include <signal.h>

#include "except.hpp"
#include "fd.hpp"
#include "pipe.hpp"

int main(void) {
  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGPIPE);

  sigprocmask(SIG_BLOCK, &sigs, nullptr);

  try {
    psx::OwnedFd fd(open(__FILE__, O_RDONLY));
    std::cout << __FILE__ << std::endl;

    psx::Pipe p;
    psx::WriteEnd w = p.write_end();

    auto [r1, _] = std::move(p);

    {
      auto r = p.read_end();

      int x = 67;
      w.write(&x, sizeof(x));

      int y;
      r.read(&y, sizeof(y));

      std::cout << x << std::endl;
    }

    return 0;
  } catch (const psx::PosixException &pe) {
    std::cerr << "ERROR: " << pe.what() << std::endl;

    return 1;
  }
}
