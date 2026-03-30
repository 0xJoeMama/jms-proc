#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "except.hpp"
#include "pipe.hpp"

struct Makis {
  int x;
  float y;
  char name[20];
};

int main(void) {
  if (unlink("fifo") < 0 && errno != ENOENT) {
    perror("bad stuff happened");
    abort();
  }

  // in parent
  if (mkfifo("fifo", 0600) < 0) {
    perror("bad stuff happened");
    abort();
  }

  int fd = open("fifo", O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    perror("bad stuff happened");
    abort();
  }

  struct pollfd p = {fd, POLLIN, 0};

  printf("Passed this\n");
  sigset_t signal;
  sigemptyset(&signal);
  int po = ppoll(&p, 1, nullptr, &signal);
  if (po < 0) {
    perror("bad stuff");
    abort();
  }

  char c[1024];
  ssize_t re = read(fd, c, sizeof(c));
  c[re] = 0;
  printf("Read '%s'\n", c);

  try {
    unlink("myfifo");
    pipes::Fifo fifo("myfifo");
    if (fork()) {
      pipes::ReadEnd r(std::move(fifo));
      Makis m;
      std::cout << "Reading..." << std::endl;
      r.read_all(m);
      std::cout << m.name << std::endl;
      std::cout << m.x << std::endl;
      std::cout << m.y << std::endl;
    } else {
      pipes::WriteEnd w(std::move(fifo));
      Makis m{12, 3.14f, "Makis had a lamb"};
      w.write_all(m);
    }
  } catch (const PosixException &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
