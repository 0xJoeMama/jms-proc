#pragma once

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <tuple>
#include <type_traits>
#include <unistd.h>

#include "except.hpp"
#include "fd.hpp"
#include "traits.hpp"

namespace psx {
// TODO: we really need to make a compilation unit for this instead of just
// TODO: inlining everything
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

class Fifo {
  std::string path;

  void mkfifo(bool expect_there) {
    while (::mkfifo(path.c_str(), 0644) < 0) {
      if (expect_there && errno == EEXIST)
        return;

      if (errno == EEXIST) {
        unlink(path.c_str());
      } else {
        std::ostringstream os{"could not create FIFO at "};
        os << path;

        throw ErrnoException(os.str());
      }
    }
  }

  OwnedFd open_fifo(int flags) {
    for (;;) {
      int fd = open(path.c_str(), flags);
      if (fd < 0) {
        if (errno == EINTR || errno == ENOENT)
          continue;

        std::ostringstream os{};
        os << "could not open FIFO at " << path;
        throw ErrnoException(os.str());
      }

      OwnedFd ofd(fd);
      struct stat st;
      if (fstat(ofd.get_fd(), &st) < 0)
        throw ErrnoException("could not stat fifo");

      if (!(st.st_mode & S_IFIFO)) {
        std::ostringstream os{};
        os << "invalid node type at " << path << ": expected S_IFIFO("
           << S_IFIFO << ") but got " << st.st_mode << "instead";

        throw FifoError(os.str());
      }

      return ofd;
    }
  }

public:
  Fifo(std::string_view path, bool expect_there) : path(path) {
    mkfifo(expect_there);
  }

  ReadEnd into_read(int flags = O_NONBLOCK) {
    return open_fifo(flags | O_RDONLY);
  }

  WriteEnd into_write(int flags = O_NONBLOCK) {
    return open_fifo(flags | O_WRONLY);
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
