#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

  return 0;
}
