#pragma once

#include <cerrno>
#include <cstring>
#include <exception>

class FullPoolException : std::exception {
public:
  const char *what() const noexcept override { return "Job pool was full"; }
};

class PosixException : std::exception {
  int err;
  const char *err_msg;

public:
  PosixException() noexcept : err(errno), err_msg(strerror(err)) {}
  const char *what() const noexcept override { return err_msg; }
  int get_errno() const { return err; }
};

class NoCommandException : std::exception {
public:
  const char *what() const noexcept override {
    return "No command was specified";
  }
};
