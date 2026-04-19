#include <csignal>

#include "except.hpp"
#include "signals.hpp"

namespace jms::signals {
void block_unwanted() {
  static struct sigaction old;

  struct sigaction act{};
  act.sa_handler = SIG_IGN;
  sigemptyset(&act.sa_mask);

  if (sigaction(SIGPIPE, &act, &old) < 0)
    throw psx::ErrnoException("failed to ignore SIGPIPE");
}

std::array<std::atomic<int>, SignalScope::N_SIGS> SignalScope::fds{};
} // namespace jms::signals
