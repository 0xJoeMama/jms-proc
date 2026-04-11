#pragma once

#include <cstring>
#include <iostream>
#include <unistd.h>

#include "except.hpp"

namespace psx {
class OwnedFd {
  int fd;

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

    if (close(fd) < 0)
      std::cerr << strerror(errno) << std::endl;
  }

  int get_fd() const { return fd; }
};
} // namespace psx
