#pragma once

#include <chrono>
#include <sys/wait.h>

#include "args.hpp"
#include "async_ops.hpp"
#include "command.hpp"
#include "executor.hpp"
#include "file_store.hpp"
#include "pipe.hpp"
#include "poll.hpp"

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

int pool_main(PoolId id, size_t jobs_per_pool, const store::FileStore &store,
              psx::DuplexConnection &conn);

class PoolHandle {
  psx::DuplexConnection conn;
  psx::Process proc;
  PoolId id;

  static PoolHandle make_pool(const store::FileStore &store, PoolId id,
                              size_t njobs);

  PoolHandle(PoolId id, psx::Process proc, psx::DuplexConnection conn)
      : conn(std::move(conn)), proc(std::move(proc)), id(id) {}

public:
  PoolHandle(const store::FileStore &store, PoolId id, size_t njobs)
      : PoolHandle(make_pool(store, id, njobs)) {}

  psx::Process &get_proc() { return proc; }
  psx::DuplexConnection &get_conn() { return conn; }
  PoolId pool_id() { return id; }

  auto get_proc_count() {
    return jms::send_command(conn, "proc-count").map([](std::string res) {
      return std::stoi(res);
    });
  }
};

namespace chr = std::chrono;
using Timepoint = chr::time_point<chr::system_clock>;

class CoordExecutor {
  const size_t jobs_per_pool;
  store::FileStore &store;
  std::map<PoolId, PoolHandle> pools;
  std::map<pid_t, PoolHandle *> by_pid;

  std::multimap<Timepoint, JobId> submission_times;
  PoolHandle *last_pool;

  JobId next_job;
  PoolId next_pool;

public:
  CoordExecutor(size_t jobs_per_pool, store::FileStore &store);

  std::map<PoolId, PoolHandle> &get_pools() { return pools; }
  const std::map<PoolId, PoolHandle> &get_pools() const { return pools; }
  PoolId get_next_pool() const { return next_pool; }
  size_t get_pool_size() const { return jobs_per_pool; }
  size_t get_total_jobs() const { return next_job; }
  const auto &get_submission_times() const { return submission_times; }

  PoolHandle *pool_for_job(JobId id);

  psx::Dynamic<std::pair<JobId, pid_t>>
  spawn_job(std::string path, std::vector<std::string> args);

  size_t terminate_pool(PoolId pool_id);
  void receive_sigchld();

private:
  psx::Dynamic<std::pair<JobId, pid_t>>
  spawn_on_last_pool(JobId id, std::string path, std::vector<std::string> args);

  PoolHandle &new_pool();
};

namespace command::coord {
class Submit : public jms::Command<CoordExecutor> {
  std::string path;
  std::vector<std::string> args;

public:
  explicit Submit(PeekableStream &peek)
      : jms::Command<CoordExecutor>(mnem::SUBMIT, peek) {
    if (!(peek.stream() >> path))
      throw error::CommandParseError("no program path was provided to submit");

    std::string s;
    while (peek.stream() >> s)
      args.emplace_back(std::move(s));
  }

  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShutdownException {
  size_t n_active;
  size_t n_finished;
};

struct Shutdown : public jms::Command<CoordExecutor> {
  explicit Shutdown(PeekableStream &peek)
      : jms::Command<CoordExecutor>(mnem::SHUTDOWN, peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShowPools : public jms::Command<CoordExecutor> {
  explicit ShowPools(PeekableStream &peek)
      : jms::Command<CoordExecutor>(mnem::SHOW_POOLS, peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShowActive : public jms::Command<CoordExecutor> {
  explicit ShowActive(PeekableStream &peek)
      : jms::Command<CoordExecutor>(mnem::SHOW_ACTIVE, peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShowFinished : public jms::Command<CoordExecutor> {
  explicit ShowFinished(PeekableStream &peek)
      : jms::Command<CoordExecutor>(mnem::SHOW_FINISHED, peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct Resume : public jms::JobIdCommand<CoordExecutor> {
  explicit Resume(PeekableStream &peek)
      : jms::JobIdCommand<CoordExecutor>(mnem::RESUME, peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct Suspend : public jms::JobIdCommand<CoordExecutor> {
  explicit Suspend(PeekableStream &peek)
      : jms::JobIdCommand<CoordExecutor>(mnem::SUSPEND, peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct Status : public jms::JobIdCommand<CoordExecutor> {
  explicit Status(PeekableStream &peek)
      : jms::JobIdCommand<CoordExecutor>(mnem::STATUS, peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct StatusAll : public jms::Command<CoordExecutor> {
  Timepoint since;

  explicit StatusAll(PeekableStream &peek)
      : jms::Command<CoordExecutor>(mnem::STATUS_ALL, peek) {
    size_t n;
    if (peek.stream() >> n) {
      since = chr::system_clock::now() - chr::seconds(n);
    } else {
      since = Timepoint::min();
    }
  }

  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};
} // namespace command::coord
} // namespace jms
