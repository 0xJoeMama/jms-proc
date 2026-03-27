#pragma once

#include <stdexcept>

#include "traits.h"

namespace pipes {
class PipeUpgradeException : std::runtime_error {
public:
  PipeUpgradeException(const char *msg) : std::runtime_error(msg) {}
};

class Pipe : public io::Read, public io::Write {
  int fds[2];

public:
  static constexpr size_t READ_END = 0;
  static constexpr size_t WRITE_END = 1;

  Pipe();
  Pipe(const Pipe &) = delete;
  ~Pipe();

  virtual size_t read(void *buf, size_t n) override;
  virtual size_t write(const void *buf, size_t n) override;

  friend class ReadEnd;
  friend class WriteEnd;
};

class ReadEnd : public io::Read {
  int fd;

public:
  ReadEnd(Pipe &&pipe);
  ReadEnd(const ReadEnd &pipe) = delete;
  ~ReadEnd();

  virtual size_t read(void *buf, size_t n) override;
};

class WriteEnd : public io::Write {
  int fd;

public:
  WriteEnd(Pipe &&pipe);
  WriteEnd(const WriteEnd &pipe) = delete;
  ~WriteEnd();

  virtual size_t write(const void *buf, size_t n) override;
};
} // namespace pipes
