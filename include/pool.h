#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

enum class JobStatus {
  Running,
  Suspended,
  Finished,
};

std::ostream &operator<<(std::ostream &, JobStatus status);

class Proc {
  pid_t pid;
  JobStatus status;

public:
  Proc(pid_t pid);
  Proc(const Proc &) = delete;
  Proc(Proc &&) = default;

  inline pid_t get_pid() const { return pid; }
  JobStatus get_status() const { return status; }
  void set_status(JobStatus status) { this->status = status; }
};

class JobPool {
  const size_t n;
  std::unordered_map<pid_t, Proc> jobs;

  static JobPool *any_pool;

public:
  JobPool(size_t n);
  JobPool(const JobPool &) = delete;

  const Proc &start_job(std::vector<std::string> &&cmd);

  std::optional<Proc *> get_by_pid(pid_t pid) {
    try {
      return std::make_optional(&this->jobs.at(pid));
    } catch (const std::out_of_range &) {
      return std::nullopt;
    }
  }

  void wait_all();

  static JobPool *get_pool() { return any_pool; }
};
