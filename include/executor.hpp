#pragma once

#include <cstddef>
#include <optional>

#include "poll.hpp"

namespace jms {
using JobId = size_t;
using PoolId = size_t;

template <typename T> using CommandAsync = std::unique_ptr<psx::AsyncBase<T>>;

enum class ProcStatus {
  Finished,
  Active,
  Suspended,
};

struct JobStatus {
  JobId id;
  ProcStatus status;
};

class CommandExecutor {
public:
  virtual CommandAsync<JobId> submit_job(std::optional<JobId> id,
                                         std::string path,
                                         std::vector<std::string> args) = 0;
  virtual CommandAsync<JobStatus> status(JobId id) const = 0;
  virtual CommandAsync<std::vector<JobStatus>>
  all_jobs(std::optional<ProcStatus> status = std::nullopt) const = 0;
  virtual CommandAsync<std::monostate> send_signal(JobId id, int sig) = 0;
};
} // namespace jms
