#include <cassert>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>

#include "pipe.hpp"
#include "poll.hpp"

template <typename T> class ReadItem : public psx::Async<T, ReadItem<T>> {
  psx::ReadEnd r;

public:
  ReadItem(psx::ReadEnd &&r) : r(std::move(r)) {}

  psx::PollStatus<T> poll(const struct pollfd &) override {
    T t;
    r.read_all<T>(t);
    std::cout << "Running we got " << t << std::endl;
    return std::move(t);
  }

  void make_wakers(psx::WakerCollector &collector) override {
    int fd = r.get_fd();
    collector(pollfd{
        fd,
        POLLIN,
        0,
    });
  }
};

int main(void) {
  psx::Pipe p;

  if (!fork()) {
    psx::WriteEnd w = p.into_write();

    int x = 674269;
    w.write_all(x);
    exit(0);
  }

  psx::ReadEnd r = p.into_read();
  ReadItem<int> it(std::move(r));

  auto res = psx::await(std::move(it));
  auto resa = psx::await(psx::select(
      ReadItem<int>(std::move(r)), ReadItem<float>(std::move(r)),
      ReadItem<long>(std::move(r)), ReadItem<std::string>(std::move(r))));

  psx::select_visit(Match{[](int) { return std::monostate{}; },
                          [](long) { return std::monostate{}; },
                          [](float) { return std::monostate{}; },
                          [](std::string) { return std::monostate{}; }},
                    std::move(resa));

  assert(res == 674269);

  it.map([](auto &&) {
    std::cout << "Magic happened" << std::endl;
    return std::monostate{};
  });

  return 0;
}
