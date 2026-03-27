#include <unistd.h>

namespace io {
class Read {
  virtual size_t read(char *buf, size_t n) = 0;
};

class Write {
  virtual size_t write(const char *buf, size_t n) = 0;
};
}
