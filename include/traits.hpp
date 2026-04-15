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

};

class Write {
public:
  virtual size_t write(const void *buf, size_t n) = 0;
};
} // namespace io
