#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

#include "except.hpp"
#include "pipe.hpp"

using namespace pipes;

namespace {
size_t read_from_pipe_fd(int fd, void *buf, size_t n) {
  ssize_t res = ::read(fd, buf, n);

  if (res < 0)
    throw PosixException();

  return res;
}

size_t write_to_pipe_fd(int fd, const void *buf, size_t n) {
  ssize_t res = ::write(fd, const_cast<void *>(buf), n);

  if (res < 0)
    throw PosixException();

  return res;
}

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

Pipe::Pipe() {
  if (pipe2(fds, O_CLOEXEC) < 0)
    throw PosixException();
}

size_t Pipe::read(void *buf, size_t n) {
  return read_from_pipe_fd(fds[READ_END], buf, n);
}

size_t Pipe::write(const void *buf, size_t n) {
  return write_to_pipe_fd(fds[WRITE_END], buf, n);
}

Pipe::~Pipe() {
  for (size_t i = 0; i < sizeof(fds) / sizeof(int); i++)
    if (fds[i] >= 0 && close(fds[i]) < 0)
      std::cerr << "WARN: failed to close pipe: " << strerror(errno);
}

Fifo::Fifo(std::string &&path, bool volatil)
    : path(std::move(path)), volatil(volatil) {
  if (mkfifo(this->path.c_str(), 0644))
    throw PosixException();
}

Fifo::~Fifo() noexcept {
  if (volatil && unlink(path.c_str()) < 0) {
    std::cerr << "WARN: could not remove fifo: " << strerror(errno)
              << std::endl;
  }
}

ReadEnd::ReadEnd(Pipe &&pipe) : fd(get_pipe_end(Pipe::READ_END, pipe.fds)) {}
ReadEnd::~ReadEnd() { close_with_warn(fd); }

size_t ReadEnd::read(void *buf, size_t n) {
  return read_from_pipe_fd(fd, buf, n);
}

WriteEnd::WriteEnd(Pipe &&pipe) : fd(get_pipe_end(Pipe::WRITE_END, pipe.fds)) {}
WriteEnd::~WriteEnd() { close_with_warn(fd); }

size_t WriteEnd::write(const void *buf, size_t n) {
  return write_to_pipe_fd(fd, buf, n);
}
