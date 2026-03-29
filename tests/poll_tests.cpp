#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <sys/poll.h>

int main(void) {
  int fds[2];

  pipe(fds);
  struct pollfd pfd{fds[0], POLLIN, 0};

  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGINT);

  int ev_cnt = ppoll(&pfd, 1, nullptr, &sigs);

  if (ev_cnt < 0) {
    perror("ppoll");
  }

  if (errno == EINTR) {
    siginfo_t info;
    sigwaitinfo(&sigs, &info);
    std::cout << "WE JUST GOT SIGINT!!!" << std::endl;
  }

  return 0;
}
