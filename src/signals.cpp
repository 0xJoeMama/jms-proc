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

SignalScope::~SignalScope() noexcept {
  for (size_t i = 0; i < olds.size(); i++) {
    std::optional<struct sigaction> old = std::nullopt;
    olds[i].swap(old);

    if (!old)
      continue;

    sigaction(static_cast<int>(i), &*old, nullptr);
    fds[i].store(-1, std::memory_order_release);
  }
}

void SignalScope::handle_signal(int sig, siginfo_t *, void *) {
  int saved = errno;

  int fd = fds[sig].load(std::memory_order_acquire);

  if (fd >= 0) {
    ssize_t res = ::write(fd, "\n", 1);
    (void)res;
  }

  errno = saved;
}
} // namespace jms::signals
