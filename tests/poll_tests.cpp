#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "except.hpp"
#include "pipe.hpp"
#include "poll.hpp"

template <typename T> class ReadItem : public psx::Pollable {
  psx::ReadEnd r;

public:
  ReadItem(psx::ReadEnd &&r) : r(std::move(r)) {}

  psx::PollStatus poll(short) override {
    try {
      T t;
      r.read_all<T>(t);
      return psx::PollStatus::Continue;
    } catch (const psx::EofException &) {
      return psx::PollStatus::Stop;
    } catch (const psx::ErrnoException &pe) {
      std::cerr << "Failed to read anything" << std::endl;
      return psx::PollStatus::Stop;
    }
  }

  short get_events() const { return POLLIN; }
  int get_fd() const override { return r.get_fd(); }
};

int main(void) {
  psx::Pipe p;

  if (!fork()) {
    psx::WriteEnd w = p.into_write();

    int x = 674269;
    std::cout << "I wrote " << x << std::endl;
    w.write_all(x);
    exit(0);
  }

  psx::ReadEnd r = p.into_read();
  ReadItem<int> it(std::move(r));
  it.get_fd();
  psx::Poll poll;
  poll.subscribe(std::move(it));

  while (poll.should_continue()) {
    poll.poll();
    while (wait(NULL) > 0)
      ;
  }

  return 0;
}
