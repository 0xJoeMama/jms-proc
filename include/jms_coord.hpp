#pragma once

#include <optional>
#include <sys/wait.h>

#include "args.hpp"
#include "executor.hpp"
#include "file_store.hpp"
#include "pipe.hpp"

namespace jms {
struct RunPath : RequiredArg<StringArg, RunPath> {
  static constexpr const char *name = "run-path";
  static constexpr const char shortname = 'l';
  static constexpr const char *desc = "Name of directory for runtime files";
};

struct JobsPerPool : RequiredArg<IntArg, JobsPerPool> {
  static constexpr const char *name = "pool-jobs";
  static constexpr const char shortname = 'n';
  static constexpr const char *desc = "Number of jobs per job pool";
};

struct CoordConfig
    : ::Command<CoordConfig, Field<RunPath>, Field<JobsPerPool>, Field<Help>> {
  static constexpr const char *desc = "Job Management System Coordinator";
  static constexpr const char *signature =
      "-l <run-directory> -n <jobs-per-pool>";
  static constexpr const char *example = "-l . -n 12";
};

int pool_main(psx::DuplexConnection &conn);

class PoolHandle {
  PoolId id;
  psx::Process proc;

  static psx::DuplexConnection make_connection(const store::FileStore &store,
                                               PoolId id) {
    std::ostringstream os{};
    os << "pool-" << id;

    auto pool_spec = store.substore(os.str());

    auto r = pool_spec.create_fifo("in").into_read();
    auto w = pool_spec.create_fifo("out").into_write();

    return {std::move(r), std::move(w)};
  }

public:
  PoolHandle(const store::FileStore &store, PoolId id)
      : id(id), proc(make_connection(store, id), pool_main) {}

  psx::Process &get_proc() { return proc; }
  PoolId pool_id() { return id; }
};

class CoordExecutor : public CommandExecutor {
  std::map<PoolId, PoolHandle> pools;
  std::optional<PoolHandle *> last_pool;

  JobId next_job;
  PoolId next_pool;

public:
  CommandAsync<JobId> submit_job(std::optional<JobId> id, std::string path,
                                 std::vector<std::string> args) override;
  CommandAsync<JobStatus> status(JobId id) const override;
  CommandAsync<std::vector<JobStatus>>
  all_jobs(std::optional<ProcStatus> status) const override;
  CommandAsync<std::monostate> send_signal(JobId id, int sig) override;
};
} // namespace jms
