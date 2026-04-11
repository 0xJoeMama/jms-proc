#pragma once

#include <cstdlib>
#include <optional>
#include <csignal>
#include <map>
#include <sys/wait.h>

#include "config.hpp"
#include "file_store.hpp"
#include "pipe.hpp"

namespace jms {
using PoolId = size_t;

class PoolHandle {
  PoolId pool_id;
  pid_t pid;
  psx::ReadEnd r;
  psx::WriteEnd w;

public:
  PoolHandle(PoolId pool_id, pid_t pid, psx::ReadEnd &&r, psx::WriteEnd &&w)
      : pool_id(pool_id), pid(pid), r(std::move(r)), w(std::move(w)) {}

  ~PoolHandle() noexcept {
    if (pid < 0) 
      return;

    if (sigqueue(pid, SIGTERM, {}) < 0) {
      std::cerr << "WARN: failed to queue SIGTERM to pool process: " << strerror(errno) << std::endl;
      return;
    }

    int exit;
    int res;
    while ((res = waitpid(pid, &exit, 0)) < 0 && errno == EINTR) 
      ;

    if (res < 0)
      std::cerr << "WARN: could not wait for child process: " << strerror(errno) << std::endl;
    else
      std::cout << "Pool " << pool_id << " exited with status " << WEXITSTATUS(exit) << std::endl;
  }
};

class Coord {
  const CoordConfig cfg;
  store::FileStore store;
  psx::ReadEnd jms_in;
  psx::Fifo jms_out;

  std::map<PoolId, PoolHandle> pools;
  std::optional<PoolHandle *> last_pool;
  PoolId next_pool;
  size_t next_job;

public:
  explicit Coord(CoordConfig &&cfg);

  void run();
  std::string execute(std::string &&command);
  PoolHandle &new_pool();
};
} // namespace jms

