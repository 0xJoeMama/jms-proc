#pragma once

#include "except.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include <signal.h>

namespace jms::signals {
void block_unwanted();

template <int sig>
class OnSignal : public psx::Async<std::monostate, OnSignal<sig>> {
  static psx::Pipe pipe;

  static void handle_signal(int, siginfo_t *, void *) {
    auto &w = pipe.write_end();
    w.write("\n", 1);
  }

public:
  OnSignal() {
    struct sigaction action;
    action.sa_sigaction = OnSignal<sig>::handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;

    if (sigaction(sig, &action, nullptr) < 0)
      throw psx::ErrnoException("failed to set signal handler");
  }

  psx::PollStatus<std::monostate> poll(const struct pollfd &) override {
    auto &r = pipe.read_end();

    char c;
    try {
      r.read(&c, 1);
    } catch (const psx::PosixException &) {
    }

    return std::monostate{};
  }

  void make_wakers(psx::WakerCollector &collector) override {
    collector(pollfd{
        pipe.read_end().get_fd(),
        POLLIN,
        0,
    });
  }
};

template <int sig>
psx::Pipe OnSignal<sig>::pipe = psx::Pipe(O_NONBLOCK | O_CLOEXEC);
}
