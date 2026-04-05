#pragma once

#include <iostream>
#include <memory>
#include <poll.h>
#include <vector>

#include "except.hpp"
#include "fd.hpp"
#include "util.hpp"

namespace psx {
enum class PollStatus {
  Stop,
  Continue,
};

class Pollable : public HasFd {
public:
  virtual PollStatus poll(short events) = 0;
  virtual short get_events() const = 0;
};

class Poll {
  std::vector<struct pollfd> pfds;
  std::vector<std::unique_ptr<Pollable>> polls;

public:
  template <typename T> void subscribe(T &&p) {
    std::unique_ptr<Pollable> owned = std::make_unique<T>(std::move(p));
    auto &res = polls.emplace_back(std::move(owned));

    struct pollfd pfd = {
        res->get_fd(),
        res->get_events(),
        0,
    };

    pfds.push_back(pfd);
  }

  bool should_continue() const { return polls.size() > 0; }

  void poll() {
    int evs = ::poll(pfds.data(), pfds.size(), -1);
    if (evs < 0)
      throw ErrnoException("failed to poll io events");

    for (size_t i = 0; i < pfds.size() && evs > 0;) {
      if (pfds[i].revents == 0) {
        i++;
        continue;
      }

      PollStatus status = polls[i]->poll(pfds[i].revents);
      pfds[i].revents = 0;

      if (status == PollStatus::Stop) {
        std::swap(pfds[i], pfds.back());
        std::swap(polls[i], polls.back());
        pfds.pop_back();
        polls.pop_back();
      }

      evs--;
    }
  }
};
} // namespace psx
