#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <variant>
#include <vector>

#include "except.hpp"
#include "pipe.hpp"
#include "util.hpp"

namespace psx {
struct Continue {};

class WakerCollector {
protected:
  std::vector<struct pollfd> &pfds;

public:
  WakerCollector(std::vector<struct pollfd> &pfds) : pfds(pfds) {}
  virtual void operator()(const struct pollfd &pfd) { pfds.push_back(pfd); }
};

template <typename T> using PollStatus = std::variant<T, Continue>;

template <typename T> class AsyncBase {
public:
  using Result = T;

  virtual PollStatus<T> poll(const struct pollfd &fd) = 0;
  virtual void make_wakers(WakerCollector &collector) = 0;

  virtual ~AsyncBase() = default;
};

template <typename T, typename Self> class Async : public AsyncBase<T> {
public:
  template <typename Fn> auto then(Fn fn);
  template <typename Fn> auto map(Fn fn);
};

template <typename T> class Dynamic : public Async<T, Dynamic<T>> {
  std::unique_ptr<AsyncBase<T>> base;

public:
  template <typename U>
  Dynamic(U &&base) : base(std::make_unique<U>(std::move(base))) {}

  PollStatus<T> poll(const struct pollfd &fd) override {
    return base->poll(fd);
  }

  void make_wakers(WakerCollector &collector) override {
    base->make_wakers(collector);
  }
};

template <typename F1, typename F2>
class Then : public Async<typename F2::Result, Then<F1, F2>> {
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

    if (std::holds_alternative<Continue>(status))
      return Continue{};

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

template <typename F, typename Fn>
class Map : public Async<std::invoke_result_t<Fn, typename F::Result &&>,
                         Map<F, Fn>> {
  F f;
  Fn fn;

public:
  using Result = std::invoke_result_t<Fn, typename F::Result &&>;

  Map(F &&prev, Fn fn) : f(std::move(prev)), fn(std::move(fn)) {}

  PollStatus<Result> poll(const struct pollfd &fd) override {
    return std::visit(
        Match{
            [](Continue) { return PollStatus<Result>(Continue{}); },
            [&](auto &&it) { return PollStatus<Result>(fn(std::move(it))); },
        },
        f.poll(fd));
  }

  void make_wakers(WakerCollector &collector) override {
    f.make_wakers(collector);
  }
};

template <typename T, typename Self>
template <typename Fn>
auto Async<T, Self>::then(Fn fn) {
  using U = std::invoke_result_t<Fn, T &&>;
  return Then<Self, U>(std::move(static_cast<Self &>(*this)), fn);
}

template <typename T, typename Self>
template <typename Fn>
auto Async<T, Self>::map(Fn fn) {
  return Map<Self, Fn>(std::move(static_cast<Self &>(*this)), fn);
}

template <typename U> class Join : public Async<std::vector<U>, Join<U>> {
  std::vector<std::unique_ptr<AsyncBase<U>>> polls;
  std::vector<U> results;
  std::multimap<int, size_t> rev;

  class Collector : public WakerCollector {
    size_t curr;
    std::multimap<int, size_t> &rev;

  public:
    Collector(size_t curr, std::multimap<int, size_t> &rev,
              const WakerCollector &collector)
        : WakerCollector(collector), curr(curr), rev(rev) {}

    void operator()(const struct pollfd &pfd) override {
      WakerCollector::operator()(pfd);
      rev.emplace(pfd.fd, curr);
    }
  };

public:
  using Result = std::vector<U>;

  template <typename T> void push(T &&it) {
    polls.emplace_back(std::make_unique<T>(std::move(it)));
  }

  PollStatus<Result> poll(const struct pollfd &fd) override {
    if (polls.empty())
      return std::move(results);

    // we received an event for fd->fd
    auto it = rev.find(fd.fd);
    if (it == rev.end()) {
      std::ostringstream os{};
      os << "file descriptor " << fd.fd << " was not part of the join set [";
      for (const auto &[fd, idx] : rev)
        os << fd << " ";

      os << "]";

      throw std::runtime_error(os.str());
    }

    auto &poll = polls[it->second];
    auto res = poll->poll(fd);

    if (std::holds_alternative<Continue>(res))
      return Continue{};

    results.emplace_back(std::get<U>(std::move(res)));

    std::swap(polls.back(), polls[it->second]);
    polls.pop_back();

    if (polls.empty())
      return std::move(results);
    else
      return Continue{};
  }

  void make_wakers(WakerCollector &collector) override {
    rev.clear();

    for (size_t i = 0; i < polls.size(); i++) {
      Collector proxy(i, rev, collector);
      polls[i]->make_wakers(proxy);
    }
  }
};

template <typename A, typename B>
class Select
    : public Async<std::variant<typename A::Result, typename B::Result>,
                   Select<A, B>> {
  A a;
  B b;

  std::set<int> a_fds;

  struct FdCollector : WakerCollector {
    std::set<int> &fds;

    FdCollector(std::set<int> &fds, const WakerCollector &collector)
        : WakerCollector(collector), fds(fds) {}

    void operator()(const struct pollfd &pfd) override {
      WakerCollector::operator()(pfd);
      fds.insert(pfd.fd);
    }
  };

public:
  using Result = std::variant<typename A::Result, typename B::Result>;

  Select(A &&a, B &&b) : a(std::move(a)), b(std::move(b)), a_fds{} {}

  PollStatus<Result> poll(const struct pollfd &fd) override {
    if (a_fds.find(fd.fd) != a_fds.end()) {
      return std::visit(
          Match{[](typename A::Result a) {
                  return PollStatus<Result>(Result(std::move(a)));
                },
                [](Continue) { return PollStatus<Result>(Continue{}); }},
          a.poll(fd));
    } else {
      return std::visit(
          Match{[](typename B::Result b) {
                  return PollStatus<Result>(Result(std::move(b)));
                },
                [](Continue) { return PollStatus<Result>(Continue{}); }},
          b.poll(fd));
    }
  }

  void make_wakers(WakerCollector &collector) override {
    a_fds.clear();
    FdCollector sub(a_fds, collector);
    a.make_wakers(sub);
    b.make_wakers(collector);
  }
};

template <typename T> class Completed : public psx::Async<T, Completed<T>> {
  T t;
  OwnedFd ev_fd;

public:
  Completed(T t)
      : t(std::move(t)), ev_fd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
    uint64_t i = 1;
    if (write(ev_fd.get_fd(), &i, sizeof(uint64_t)) < 0)
      throw ErrnoException(
          "could not create event file descriptor for completed future");
  }

  PollStatus<T> poll(const struct pollfd &) override { return std::move(t); }
  void make_wakers(WakerCollector &collector) override {
    collector(pollfd{ev_fd.get_fd(), POLLIN, 0});
  }
};

template <typename... Ts> auto select(Ts &&...ts);

template <typename A, typename B> auto select(A &&a, B &&b) {
  return Select(std::move(a), std::move(b));
}

template <typename A, typename... Rest> auto select(A &&a, Rest &&...rest) {
  return Select(std::move(a), select<Rest...>(std::move(rest)...));
}

namespace details {
template <typename T> struct is_variant;

template <typename... Ts>
struct is_variant<std::variant<Ts...>> : std::true_type {};

template <typename T> struct is_variant : std::false_type {};
} // namespace details

template <typename Visitor, typename A>
void select_visit(const Visitor &visitor, A &&a) {
  if constexpr (details::is_variant<A>::value) {
    std::visit([&](auto &&v) { select_visit(visitor, std::move(v)); },
               std::move(a));
  } else {
    visitor(std::move(a));
  }
}

// block on AsyncBase it until it is completed
template <typename U> typename U::Result await(U &&it) {
  using T = typename U::Result;

  std::vector<struct pollfd> wakers;
  WakerCollector collector(wakers);

  it.make_wakers(collector);
  // TODO: perhaps make this run at least once for all async to allow the to
  // TODO: properly poll the first time around
  while (!wakers.empty()) {
    int evs = ::ppoll(wakers.data(), wakers.size(), nullptr, nullptr);
    if (evs < 0 && errno != EINTR)
      throw ErrnoException("failed to poll file descriptor events");

    for (size_t i = 0; i < wakers.size() && evs > 0; i++) {
      const auto &curr = wakers[i];

      if (!curr.revents)
        continue;

      evs--;
      try {
        auto res = it.poll(curr);

        if (std::holds_alternative<T>(res))
          return std::get<T>(std::move(res));
        else
          break;
      } catch (const ErrnoException &e) {
        switch (e.get_errno()) {
        case EINTR:
          /* fallthrough */
        case EAGAIN:
          continue;
        default:
          throw e;
        };
      }
    }

    wakers.clear();
    it.make_wakers(collector);
  }

  struct pollfd pfd{-1, 0, 0};

  // TODO: this currently throws so we cannot have an infinitely polling async
  // (which makes sense at least right now...)
  return std::visit(
      Match{
          [](Continue &&) -> typename U::Result {
            throw std::runtime_error(
                "async had no wakers and did not completed instantly");
          },
          [](auto &&res) { return std::move(res); },
      },
      it.poll(pfd));
}
} // namespace psx
