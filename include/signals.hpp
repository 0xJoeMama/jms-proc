#pragma once

#include <atomic>
#include <signal.h>

#include "except.hpp"
#include "pipe.hpp"
#include "poll.hpp"

namespace jms::signals {
void block_unwanted();

template <int sig> struct Signal {};

template <int sig>
class OnSignal : public psx::Async<Signal<sig>, OnSignal<sig>> {
  // FIXME: This currently broadcasts signals to all pools which is NOT what we
  // want
  psx::ReadEnd &r;

public:
  OnSignal(psx::ReadEnd &r) : r(r) {}

  psx::PollStatus<Signal<sig>> poll(const struct pollfd &) override {
    char c;
    try {
      r.read(&c, 1);
    } catch (const psx::PosixException &) {
    }

    return Signal<sig>{};
  }

  void make_wakers(psx::WakerCollector &collector) override {
    collector(pollfd{
        r.get_fd(),
        POLLIN,
        0,
    });
  }
};

class SignalScope {
  static constexpr const int N_SIGS = 64;
  static std::array<std::atomic<int>, N_SIGS> fds;

  // TODO: this currently may have race conditions due to use of std::optional
  // in write_ends
  static void handle_signal(int sig, siginfo_t *, void *) {
    int saved = errno;

    int fd = fds[sig].load(std::memory_order_acquire);

    if (fd >= 0)
      ::write(fd, "\n", 1);

    errno = saved;
  }

  std::array<std::optional<psx::ReadEnd>, N_SIGS> read_ends;
  std::array<std::optional<psx::WriteEnd>, N_SIGS> write_ends;
  std::array<std::optional<struct sigaction>, N_SIGS> olds;

public:
  SignalScope() {}

  template <int sig> OnSignal<sig> on_signal() {
    if (read_ends[sig])
      return {*read_ends[sig]};

    auto [r, w] = psx::Pipe(O_CLOEXEC | O_NONBLOCK);

    read_ends[sig].emplace(std::move(r));
    write_ends[sig].emplace(std::move(w));

    struct sigaction old;
    struct sigaction action;

    action.sa_sigaction = SignalScope::handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;

    fds[sig].store(write_ends[sig]->get_fd(), std::memory_order_release);

    if (sigaction(sig, &action, &old) < 0) {
      read_ends[sig].reset();
      write_ends[sig].reset();
      throw psx::ErrnoException("failed to set signal handler");
    }

    olds[sig].emplace(std::move(old));

    return on_signal<sig>();
  }

  ~SignalScope() noexcept {
    for (size_t i = 0; i < olds.size(); i++) {
      std::optional<struct sigaction> old = std::nullopt;
      olds[i].swap(old);

      if (!old)
        continue;

      sigaction(static_cast<int>(i), &*old, nullptr);
      fds[i].store(-1, std::memory_order_release);
    }
  }
};
} // namespace jms::signals
