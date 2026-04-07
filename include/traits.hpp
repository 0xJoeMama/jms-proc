#pragma once

#include <climits>
#include <iostream>
#include <istream>
#include <ostream>
#include <unistd.h>

namespace io {
class Read {
public:
  virtual size_t read(void *buf, size_t n) = 0;

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

class Write {
public:
  virtual size_t write(const void *buf, size_t n) = 0;

  void write_from_istream(std::istream &is, size_t n) {
    char buf[_POSIX_PIPE_BUF];
    size_t written = 0;

    while (written < n) {
      is.read(buf, std::min(static_cast<size_t>(_POSIX_PIPE_BUF), n - written));

      size_t last = is.gcount();
      size_t all = 0;

      while (all < last)
        all += this->write(buf, last);

      written += last;
    }
  }

  template <typename T> void write_all(const T &t) {
    size_t written = 0;

    // if any of the writes fail, then this function call fails
    while (written < sizeof(T))
      written += this->write(reinterpret_cast<const char *>(&t) + written,
                             sizeof(T) - written);
  }
};
} // namespace io
