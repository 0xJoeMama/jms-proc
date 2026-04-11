#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "pipe.hpp"

namespace pool {
enum class JobStatus {
  Active,
  Suspended,
  Finished,
};

std::ostream &operator<<(std::ostream &, JobStatus status);

namespace chr = std::chrono;

class Job {
  pid_t pid;
  JobStatus status;
  chr::time_point<chr::steady_clock> last_start;

public:
  Job(pid_t pid);
  Job(const Job &) = delete;
  Job(Job &&) = default;

  inline pid_t get_pid() const { return pid; }
  JobStatus get_status() const { return status; }
  void set_status(JobStatus status) { this->status = status; }
};

class JobPool {
  const size_t n;
  std::unordered_map<pid_t, Job> jobs;

public:
  JobPool(size_t n);
  JobPool(const JobPool &) = delete;

  const Job &start_job(std::vector<std::string> &&cmd);
  void wait_all();
};

int pool_main(size_t n, psx::ReadEnd r, psx::WriteEnd w);
} // namespace pool
