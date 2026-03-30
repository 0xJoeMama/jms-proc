#pragma once

#include <unistd.h>

namespace io {
class Read {
public:
  virtual size_t read(void *buf, size_t n) = 0;

  template <typename T> void read_all(T &t) {
    size_t read = 0;

    // if any of the writes fail, then this function call fails
    while (read < sizeof(T))
      read += this->read((char *)&t + read, sizeof(T) - read);
  }
};

class Write {
public:
  virtual size_t write(const void *buf, size_t n) = 0;

  template <typename T> void write_all(T &t) {
    size_t written = 0;

    // if any of the writes fail, then this function call fails
    while (written < sizeof(T))
      written += this->write((char *)&t + written, sizeof(T) - written);
  }
};
} // namespace io
