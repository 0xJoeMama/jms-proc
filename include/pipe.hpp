#pragma once

#include <array>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <tuple>
#include <type_traits>
#include <unistd.h>

#include "except.hpp"
#include "fd.hpp"
#include "traits.hpp"

namespace psx {
class Fifo {
  std::string path;

public:
  Fifo(std::string &&path) : path(std::move(path)) {}
  Fifo(const Fifo &) = delete;
};

class ReadEnd : public io::Read, HasFd {
  OwnedFd fd;

public:
  ReadEnd(OwnedFd &&fd) : fd(std::move(fd)) {}

  size_t read(void *buf, size_t n) override {
    ssize_t res = ::read(fd.get_fd(), buf, n);

    if (res < 0)
      throw ErrnoException("failed to read from pipe");
    else if (res == 0)
      throw EofException();

    return res;
  }

  int get_fd() const override { return fd.get_fd(); }
};

class WriteEnd : public io::Write, HasFd {
  OwnedFd fd;

public:
  WriteEnd(OwnedFd &&fd) : fd(std::move(fd)) {}

  size_t write(const void *buf, size_t n) override {
    ssize_t res = ::write(fd.get_fd(), const_cast<void *>(buf), n);

    if (res < 0)
      throw ErrnoException("failed to write to pipe");
    else if (res == 0)
      throw EofException();

    return res;
  }

  int get_fd() const override { return fd.get_fd(); }
};

class Pipe {
  ReadEnd read;
  WriteEnd write;

  static std::array<OwnedFd, 2> open_pipe(int flags) {
    int fds[2];
    if (pipe2(fds, flags) < 0)
      throw ErrnoException("failed to open pipe");

    return {OwnedFd(fds[0]), OwnedFd(fds[1])};
  }

  Pipe(std::array<OwnedFd, 2> fds)
      : read(std::move(fds[0])), write(std::move(fds[1])) {}

public:
  Pipe(int flags = 0) : psx::Pipe(open_pipe(flags)) {}

  ReadEnd into_read() {
    auto _ = std::move(write);
    return std::move(read);
  }

  WriteEnd into_write() {
    auto _ = std::move(read);
    return std::move(write);
  }

  template <size_t i> auto get() {
    if constexpr (i == 0) {
      return std::move(read);
    } else if constexpr (i == 1) {
      return std::move(write);
    }
  }
};

} // namespace psx

namespace std {
template <> struct tuple_size<psx::Pipe> : std::integral_constant<size_t, 2> {};
template <> struct tuple_element<0, psx::Pipe> {
  using type = psx::ReadEnd;
};

template <> struct tuple_element<1, psx::Pipe> {
  using type = psx::WriteEnd;
};
} // namespace std
