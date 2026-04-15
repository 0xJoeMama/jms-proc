#pragma once

#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "fd.hpp"
#include "traits.hpp"

namespace psx {
class ReadEnd : public io::Read {
  OwnedFd fd;

public:
  ReadEnd(OwnedFd &&fd);

  size_t read(void *buf, size_t n) override;
  int get_fd() const { return fd.get_fd(); }
};

class WriteEnd : public io::Write {
  OwnedFd fd;

public:
  WriteEnd(OwnedFd &&fd);

  size_t write(const void *buf, size_t n) override;
  int get_fd() const { return fd.get_fd(); }
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

