#include <cassert>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "except.hpp"
#include "pipe.hpp"
#include "poll.hpp"

template <typename T> class ReadItem : public psx::Async<T> {
  psx::ReadEnd r;

public:
  ReadItem(psx::ReadEnd &&r) : r(std::move(r)) {}

  psx::PollStatus<T> poll(const struct pollfd &) override {
    try {
      T t;
      r.read_all<T>(t);
      std::cout << "Running we got " << t << std::endl;
      return std::move(t);
    } catch (const psx::EofException &) {
      return psx::Stop{};
    } catch (const psx::ErrnoException &pe) {
      std::cerr << "Failed to read anything" << std::endl;
      return psx::Stop{};
    }
  }

  void make_wakers(psx::WakerCollector &collector) override {
    int fd = r.get_fd();
    collector.push(pollfd{
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

  auto res = psx::run_to_completion(std::move(it));

  assert(*res == 674269);

  return 0;
}
