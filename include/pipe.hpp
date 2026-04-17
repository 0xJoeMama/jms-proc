#pragma once

#include <climits>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>

#include "except.hpp"

namespace psx {
class OwnedFd {
  int fd;

  // TODO: move this implementation to another file
public:
  OwnedFd(int fd) : fd(fd) {
    if (fd < 0)
      throw psx::ErrnoException("invalid file descriptor");
  }

  OwnedFd(const OwnedFd &) = delete;
  OwnedFd(OwnedFd &&fd) : fd(fd.fd) { fd.fd = -1; }

  ~OwnedFd() noexcept {
    if (fd < 0)
      return;

    // TODO: move this out of here...
    if (close(fd) < 0)
      std::cerr << strerror(errno) << std::endl;
  }

  int get_fd() const { return fd; }
};

class ReadEnd {
  OwnedFd fd;

public:
  ReadEnd(OwnedFd &&fd);

  size_t read(void *buf, size_t n);

  int get_fd() const { return fd.get_fd(); }

  template <size_t N = _POSIX_PIPE_BUF>
  void read_max_to_ostream(std::ostream &os) {
    char buf[N];
    size_t last = this->read(buf, N);
    os.write(buf, last);
  }

  void read_to_ostream(std::ostream &os, size_t n) {
    char buf[_POSIX_PIPE_BUF];
    size_t read = 0;
    while (read < n) {
      size_t last = this->read(buf, _POSIX_PIPE_BUF);
      os.write(buf, last);
      read += last;
    }
  }

  template <typename T> void read_all(T &t) {
    size_t read = 0;

    // if any of the writes fail, then this function call fails
    while (read < sizeof(T))
      read += this->read(reinterpret_cast<char *>(&t) + read, sizeof(T) - read);
  }
};

class WriteEnd {
  OwnedFd fd;

public:
  WriteEnd(OwnedFd &&fd);

  size_t write(const void *buf, size_t n);
  int get_fd() const { return fd.get_fd(); }

  template <typename T> void write_all(const T &t) {
    size_t written = 0;

    // if any of the writes fail, then this function call fails
    while (written < sizeof(T))
      written += this->write(reinterpret_cast<const char *>(&t) + written,
                             sizeof(T) - written);
  }
};

class Pipe {
  ReadEnd read;
  WriteEnd write;

  Pipe(std::array<OwnedFd, 2> fds);

public:
  Pipe(int flags = 0);

  ReadEnd into_read();
  WriteEnd into_write();

  ReadEnd &read_end();
  WriteEnd &write_end();

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

public:
  Fifo(std::string_view path, bool expect_there);

  ReadEnd into_read(int flags = O_NONBLOCK);
  WriteEnd into_write(int flags = O_NONBLOCK);
};

struct DuplexConnection {
  psx::ReadEnd r;
  psx::WriteEnd w;

  DuplexConnection(psx::ReadEnd r, psx::WriteEnd w);
};

class Process {
  pid_t pid;
  DuplexConnection conn;

  Process(pid_t pid, DuplexConnection conn) : pid(pid), conn(std::move(conn)) {}

public:
  static Process fork(std::function<Fifo(std::string_view)> make_fifo,
                      std::function<int(DuplexConnection &)> f);

  Process(Process &&) noexcept;

  // blocking version
  int wait();
  std::optional<int> wait_no_block();

  DuplexConnection &get_conn() { return conn; }
  pid_t get_pid() const { return pid; }

  ~Process() noexcept;
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
