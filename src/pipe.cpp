#include <cstring>
#include <iostream>
#include <unistd.h>

#include "except.h"
#include "pipe.h"

using namespace pipes;

Pipe::Pipe() {
  if (pipe(fds) < 0)
    throw PosixException();
}

Pipe::~Pipe() {
  for (size_t i = 0; i < sizeof(fds) / sizeof(int); i++)
    if (fds[i] >= 0 && close(fds[i]) < 0)
      std::cerr << "WARN: failed to close pipe: " << strerror(errno);
}

namespace {
void close_with_warn(int fd) {
  if (close(fd) < 0)
    std::cerr << "WARN: failed to close pipe: " << strerror(errno) << std::endl;
}

int get_pipe_end(int end, int fds[2]) {
  int fd = fds[end];
  if (fd < 0 || fds[!end] < 0)
    throw PipeUpgradeException(
        "Attempted to upgrade an already upgraded or moved pipe");

  close_with_warn(fds[!end]);

  fds[0] = -1;
  fds[1] = -1;

  return fd;
}
} // namespace

ReadEnd::ReadEnd(Pipe &&pipe) : fd(get_pipe_end(Pipe::READ_END, pipe.fds)) {}
ReadEnd::~ReadEnd() { close_with_warn(fd); }

size_t ReadEnd::read(char *buf, size_t n) {
  ssize_t res = ::read(fd, buf, n);

  if (res < 0)
    throw PosixException();

  return res;
}

WriteEnd::WriteEnd(Pipe &&pipe) : fd(get_pipe_end(Pipe::WRITE_END, pipe.fds)) {}
WriteEnd::~WriteEnd() { close_with_warn(fd); }

size_t WriteEnd::write(const char *buf, size_t n) {
  ssize_t res = ::write(fd, const_cast<char *>(buf), n);

  if (res < 0)
    throw PosixException();

  return res;
}
