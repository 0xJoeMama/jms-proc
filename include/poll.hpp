#pragma once

#include "except.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <sys/poll.h>
#include <variant>
#include <vector>

namespace psx {
struct Continue {};
struct Stop {};

class WakerCollector {
protected:
  std::vector<struct pollfd> &pfds;

public:
  WakerCollector(std::vector<struct pollfd> &pfds) : pfds(pfds) {}

  virtual void push(const struct pollfd &pfd) { pfds.push_back(pfd); }
};

template <typename T> using PollStatus = std::variant<T, Continue, Stop>;

template <typename T, typename U>
PollStatus<U> status_cast(PollStatus<T> t) {
  if (std::holds_alternative<Continue>(t))
    return Continue{};
  else if (std::holds_alternative<Stop>(t))
    return Stop{};
  else
    throw std::runtime_error("invalid status cast");
}

template <typename T> class Async {
public:
  using Result = T;

  virtual PollStatus<T> poll(const struct pollfd &fd) = 0;
  virtual void make_wakers(WakerCollector &collector) = 0;
};

template <typename F1, typename F2>
class Then : public Async<typename F2::Result> {
  using First = typename F1::Result;
  using Fn = std::function<F2(First &&)>;

  F1 f1;
  std::optional<F2> f2;
  Fn fn;

public:
  using Result = typename F2::Result;

  Then(F1 &&f1, Fn fn) : f1(std::move(f1)), f2(std::nullopt), fn(fn) {}

  PollStatus<Result> poll(const struct pollfd &fd) override {
    if (f2)
      return f2->poll(fd);

    auto status = f1.poll(fd);

    if (std::holds_alternative<Continue>(status)) return Continue{};
    if (std::holds_alternative<Stop>(status)) return Stop{};

    auto f2res = fn(std::move(std::get<First>(status)));
    f2.emplace(std::move(f2res));

    return poll(fd);
  }

  void make_wakers(WakerCollector &collector) override {
    if (f2)
      f2->make_wakers(collector);
    else
      f1.make_wakers(collector);
  }
};

template <typename U> class Join : public Async<std::vector<U>> {
  std::vector<std::unique_ptr<Async<U>>> polls;
  std::vector<U> results;
  std::multimap<int, size_t> rev;

  class Collector : public WakerCollector {
    size_t curr;
    std::multimap<int, size_t> &rev;

  public:
    Collector(size_t curr, std::multimap<int, size_t> &rev,
              const WakerCollector &collector)
        : WakerCollector(collector), curr(curr), rev(rev) {}

    void push(const struct pollfd &pfd) override {
      WakerCollector::push(pfd);
      rev.emplace(curr, pfd.fd);
    }
  };

public:
  using Result = std::vector<U>;

  PollStatus<Result> poll(const struct pollfd &fd) override {
    // we received an event for fd->fd
    auto it = rev.find(fd.fd);
    if (it == rev.end())
      throw std::runtime_error("invalid file descriptor was provided to poll");

    auto &poll = polls[it->second];
    auto res = poll->poll(fd);

    if (std::holds_alternative<Continue>(res) ||
        std::holds_alternative<Stop>(res))
      return res;

    results.emplace_back(std::get<U>(std::move(res)));

    std::swap(polls.back(), polls[it->second]);
    polls.pop_back();

    if (polls.size())
      return Continue{};
    else
      return std::move(results);
  }

  void make_wakers(WakerCollector &collector) override {
    rev.clear();

    for (size_t i = 0; i < polls.size(); i++) {
      Collector proxy(collector, i, rev);
      polls[i].make_wakers(proxy);
    }
  }
};

template <typename U>
std::optional<typename U::Result> run_to_completion(U &&it) {
  using T = typename U::Result;

  std::vector<struct pollfd> wakers;
  WakerCollector collector(wakers);

  it.make_wakers(collector);
  while (!wakers.empty()) {
    int evs = ::poll(wakers.data(), wakers.size(), -1);
    if (evs < 0 && errno != EINTR)
      throw ErrnoException("failed to poll file descriptor events");

    for (size_t i = 0; i < wakers.size(); i++) {
      const auto &curr = wakers[i];

      if (!curr.revents)
        continue;

      auto res = it.poll(curr);

      if (std::holds_alternative<T>(res)) {
        return std::get<T>(res);
      } else if (std::holds_alternative<Stop>(res)) {
        return std::nullopt;
      }
    }

    wakers.clear();
    it.make_wakers(collector);
  }

  return std::nullopt;
}
} // namespace psx
