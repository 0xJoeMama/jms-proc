#pragma once

#include <cstddef>

namespace jms {
using JobId = size_t;
using PoolId = size_t;

enum class ProcStatus {
  Finished,
  Active,
  Suspended,
};

struct JobStatus {
  JobId id;
  ProcStatus status;
};

// command mnemonics
namespace command {
namespace coord::mnem {
constexpr const char *SUBMIT = "submit";
constexpr const char *STATUS = "status";
constexpr const char *STATUS_ALL = "status-all";
constexpr const char *SHUTDOWN = "shutdown";
constexpr const char *SHOW_POOLS = "show-pools";
constexpr const char *SHOW_ACTIVE = "show-active";
constexpr const char *SHOW_FINISHED = "show-finished";
constexpr const char *SUSPEND = "suspend";
constexpr const char *RESUME = "resume";
} // namespace coord::mnem

namespace pool::mnem {
constexpr const char *SUBMIT = "submit";
constexpr const char *STATUS = "status";
constexpr const char *STATUS_AFTER = "status-after";
constexpr const char *SHOW_ACTIVE = "show-active";
constexpr const char *SHOW_FINISHED = "show-finished";
constexpr const char *SUSPEND = "suspend";
constexpr const char *RESUME = "resume";
constexpr const char *PROC_COUNT = "proc-count";
} // namespace pool::mnem
} // namespace command
} // namespace jms
