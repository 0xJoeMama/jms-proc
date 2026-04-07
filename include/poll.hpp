#pragma once

#include <algorithm>
#include <memory>
#include <poll.h>
#include <vector>

#include "except.hpp"
#include "fd.hpp"

namespace psx {
enum class PollStatus {
  Done,
  Continue,
  Stop,
};

template <typename T> class Async : public HasFd {
public:
  virtual PollStatus poll(short events) = 0;
  virtual short get_events() const = 0;
  virtual T &result() = 0;
};

template <typename U> class Poll {
  std::vector<struct pollfd> pfds;
  std::vector<std::unique_ptr<Async<U>>> polls;
  std::vector<U> results;

public:
  template <typename T> void subscribe(T &&p) {
    std::unique_ptr<Async<U>> owned = std::make_unique<T>(std::move(p));
    auto &res = polls.emplace_back(std::move(owned));

    struct pollfd pfd = {
        res->get_fd(),
        res->get_events(),
        0,
    };

    pfds.push_back(pfd);
  }

  bool should_continue() const { return polls.size() > 0; }

  void run_to_completion() {
    int evs = ::poll(pfds.data(), pfds.size(), -1);
    if (evs < 0) {
      auto e = ErrnoException("failed to poll io events");
      if (e.get_errno() == EINTR)
        return;

      throw e;
    }

    for (size_t i = 0; i < pfds.size() && evs > 0;) {
      if (pfds[i].revents == 0) {
        i++;
        continue;
      }

      PollStatus status = polls[i]->poll(pfds[i].revents);
      pfds[i].revents = 0;

      switch (status) {
      case PollStatus::Done: {
        std::swap(pfds[i], pfds.back());
        std::swap(polls[i], polls.back());
        // since its done it means we can now safely retrieve our result
        results.push_back(polls.back()->result());
        pfds.pop_back();
        polls.pop_back();
      } break;
      case PollStatus::Continue: {
        // do nothing, continue polling
      } break;
      case PollStatus::Stop: {
        std::swap(pfds[i], pfds.back());
        std::swap(polls[i], polls.back());
        // if weve stopped it means result is probably not done
        pfds.pop_back();
        polls.pop_back();
      } break;
      }

      evs--;
    }
  }

  std::vector<U> finish() { return results; }
};
} // namespace psx
