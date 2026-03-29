#pragma once

#include <cerrno>
#include <cstring>
#include <exception>
#include <string>

class FullPoolException : std::exception {
public:
  const char *what() const noexcept override { return "Job pool was full"; }
};

class PosixException : std::exception {
  int err;
  std::string errmsg;

public:
  PosixException(std::string &&errmsg, int errnum)
      : err(errnum), errmsg(std::move(errmsg)) {
    this->errmsg += strerror(errnum);
  }

  PosixException(std::string &&errmsg)
      : PosixException(std::move(errmsg), errno) {}

  const char *what() const noexcept override { return errmsg.c_str(); }
  int get_errno() const { return err; }
};

class NoCommandException : std::exception {
public:
  const char *what() const noexcept override {
    return "No command was specified";
  }
};
