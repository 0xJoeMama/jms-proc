#pragma once

#include "config.hpp"
#include "file_store.hpp"
#include "pipe.hpp"

#include <optional>
#include <unordered_map>

namespace jms {
class PoolHandle {
  pid_t pid;
  psx::ReadEnd r;
  psx::WriteEnd w;
};

class Coord {
  const CoordConfig cfg;
  store::FileStore store;
  psx::ReadEnd jms_in;
  psx::Fifo jms_out;

  std::unordered_map<pid_t, PoolHandle> pools;
  std::optional<size_t> last_pool;
  size_t curr_job;

public:
  explicit Coord(CoordConfig &&cfg);
  void run();
  std::string execute(std::string &&command);
};
} // namespace jms
