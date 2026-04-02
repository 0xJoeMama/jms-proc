#pragma once

#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>

namespace psx {
class PosixException : std::exception {
  int err;
  std::string errmsg;

public:
  PosixException(int errnum) : err(errnum), errmsg(strerror(errnum)) {}
  PosixException() : psx::PosixException(errno) {}
  PosixException(std::string_view ctx, const PosixException &pe) : err(pe.err) {
    std::ostringstream os;
    os << ctx << ": " << pe.errmsg << std::endl;

    errmsg = os.str();
  }

  PosixException(std::string_view ctx) : err(errno) {
    std::ostringstream os;
    os << ctx << ": " << strerror(errno) << std::endl;

    errmsg = os.str();
  }

  const char *what() const noexcept override { return errmsg.c_str(); }
  int get_errno() const { return err; }
};
} // namespace psx

class FullPoolException : std::exception {
public:
  const char *what() const noexcept override { return "Job pool was full"; }
};

class NoCommandException : std::exception {
public:
  const char *what() const noexcept override {
    return "No command was specified";
  }
};
