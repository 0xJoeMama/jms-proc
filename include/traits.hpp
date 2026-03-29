#pragma once

#include <unistd.h>

namespace io {
class Read {
  virtual size_t read(void *buf, size_t n) = 0;
};

class Write {
  virtual size_t write(const void *buf, size_t n) = 0;
};
}
