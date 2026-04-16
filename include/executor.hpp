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
} // namespace jms
