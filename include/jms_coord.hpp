#pragma once

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

int pool_main(const store::FileStore &store, psx::DuplexConnection &conn);

class PoolHandle {
  PoolId id;
  psx::Process proc;
  psx::DuplexConnection conn;

  static PoolHandle make_pool(const store::FileStore &store, PoolId id);

  PoolHandle(PoolId id, psx::Process proc, psx::DuplexConnection conn)
      : id(id), proc(std::move(proc)), conn(std::move(conn)) {}

public:
  PoolHandle(const store::FileStore &store, PoolId id)
      : PoolHandle(make_pool(store, id)) {}

  psx::Process &get_proc() { return proc; }
  psx::DuplexConnection &get_conn() { return conn; }
  PoolId pool_id() { return id; }

  auto get_proc_count() {
    return jms::send_command(conn, "proc-count").map([](std::string res) {
      return std::stoi(res);
    });
  }
};

class CoordExecutor {
  const size_t jobs_per_pool;
  store::FileStore &store;
  std::map<PoolId, PoolHandle> pools;
  PoolHandle *last_pool;

  JobId next_job;
  PoolId next_pool;

public:
  CoordExecutor(size_t jobs_per_pool, store::FileStore &store)
      : jobs_per_pool(jobs_per_pool), store(store), next_job{0}, next_pool{0} {}

  std::map<PoolId, PoolHandle> &get_pools() { return pools; }
  const std::map<PoolId, PoolHandle> &get_pools() const { return pools; }
  PoolId get_next_pool() const { return next_pool; }
  size_t get_pool_size() const { return jobs_per_pool; }

  PoolHandle *pool_for_job(JobId id);

  auto spawn_job(std::string path, std::vector<std::string> args) {
    JobId job_id = next_job++;

    if (!last_pool || job_id % jobs_per_pool == 0)
      new_pool();

    return spawn_on_last_pool(job_id, std::move(path), std::move(args));
  }

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
      : jms::Command<CoordExecutor>("submit", peek) {
    if (!(peek.stream() >> path))
      throw error::CommandParseError("no program path was provided to submit");

    std::string s;
    while (peek.stream() >> s)
      args.emplace_back(std::move(s));
  }

  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShutdownException {};

struct Shutdown : public jms::Command<CoordExecutor> {
  explicit Shutdown(PeekableStream &peek)
      : jms::Command<CoordExecutor>("shutdown", peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShowPools : public jms::Command<CoordExecutor> {
  explicit ShowPools(PeekableStream &peek)
      : jms::Command<CoordExecutor>("show-pools", peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShowActive : public jms::Command<CoordExecutor> {
  explicit ShowActive(PeekableStream &peek)
      : jms::Command<CoordExecutor>("show-active", peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct ShowFinished : public jms::Command<CoordExecutor> {
  explicit ShowFinished(PeekableStream &peek)
      : jms::Command<CoordExecutor>("show-finished", peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct Resume : public jms::JobIdCommand<CoordExecutor> {
  explicit Resume(PeekableStream &peek)
      : jms::JobIdCommand<CoordExecutor>("resume", peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

struct Suspend : public jms::JobIdCommand<CoordExecutor> {
  explicit Suspend(PeekableStream &peek)
      : jms::JobIdCommand<CoordExecutor>("suspend", peek) {}
  psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
};

// struct Status : public jms::JobIdCommand<CoordExecutor> {
//   explicit Status(PeekableStream &peek)
//       : jms::JobIdCommand<CoordExecutor>("status", peek) {}
//   psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
// };
//
// struct StatusAll : public jms::JobIdCommand<CoordExecutor> {
//   explicit StatusAll(PeekableStream &peek)
//       : jms::JobIdCommand<CoordExecutor>("status-all", peek) {
//   }
//   psx::Dynamic<std::string> execute(CoordExecutor &ex) override;
// };
} // namespace command::coord
} // namespace jms
