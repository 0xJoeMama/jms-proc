#pragma once

#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>

namespace psx {
class PosixException : public std::exception {};

class FifoError : public PosixException {
  std::string s;

public:
  FifoError(const std::string &s) : s(s) {}
  const char *what() const noexcept override { return s.c_str(); }
};

class EofException : public PosixException {
public:
  const char *what() const noexcept override { return "Reached End of File"; }
};

class ErrnoException : public PosixException {
  int err;
  std::string errmsg;

public:
  ErrnoException(int errnum) : err(errnum), errmsg(strerror(errnum)) {}
  ErrnoException() : psx::ErrnoException(errno) {}
  ErrnoException(std::string_view ctx, const ErrnoException &pe) : err(pe.err) {
    std::ostringstream os;
    os << ctx << ": " << pe.errmsg << std::endl;

    errmsg = os.str();
  }

  ErrnoException(std::string_view ctx) : err(errno) {
    std::ostringstream os;
    os << ctx << ": " << strerror(errno) << std::endl;

    errmsg = os.str();
  }

  const char *what() const noexcept override { return errmsg.c_str(); }
  int get_errno() const { return err; }
};
} // namespace psx
