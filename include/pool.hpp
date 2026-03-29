#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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

  std::optional<Job *> get_by_pid(pid_t pid) {
    try {
      return std::make_optional(&this->jobs.at(pid));
    } catch (const std::out_of_range &) {
      return std::nullopt;
    }
  }

  void wait_all();
};
} // namespace pool
