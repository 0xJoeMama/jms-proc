#include <csignal>

#include "signals.hpp"
#include "except.hpp"

void jms::signals::block_unwanted() {
  static struct sigaction old;

  struct sigaction act{};
  act.sa_handler = SIG_IGN;
  if (sigaction(SIGPIPE, &act, &old) < 0) 
    throw psx::ErrnoException("failed to ignore SIGPIPE");
}
