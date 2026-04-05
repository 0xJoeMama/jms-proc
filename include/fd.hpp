#pragma once

#include <cstring>
#include <iostream>
#include <unistd.h>

#include "except.hpp"

namespace psx {
class HasFd {
public:
  virtual int get_fd() const = 0;
};

class OwnedFd : public HasFd {
  int fd;

public:
  OwnedFd(int fd) : fd(fd) {
    if (fd < 0)
      throw psx::ErrnoException();
  }

  OwnedFd(const OwnedFd &) = delete;
  OwnedFd(OwnedFd &&fd) : fd(fd.fd) { fd.fd = -1; }

  ~OwnedFd() noexcept {
    if (fd < 0)
      return;

    if (close(fd) < 0)
      std::cerr << strerror(errno) << std::endl;
  }

  int get_fd() const override { return fd; }
};

class Fd : public HasFd {
  OwnedFd &owned;

public:
  Fd(OwnedFd &owned) : owned(owned) {}
  Fd(const Fd &other) : owned(other.owned) {}
  Fd(Fd &&) = delete;

  int get_fd() const override { return owned.get_fd(); }
};
} // namespace psx
