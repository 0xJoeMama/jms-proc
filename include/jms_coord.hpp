#pragma once

#include <optional>
#include <sys/wait.h>

#include "args.hpp"
#include "async_ops.hpp"
#include "command.hpp"
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

  auto get_proc_count() {
    return WriteString("proc-count", proc.get_conn().w)
        .then([this](auto) { return ReadString(proc.get_conn().r); })
        .map([](std::string &&s) { return std::stoi(s); });
  }
};

class CoordExecutor {
  size_t jobs_per_pool;
  store::FileStore &store;
  std::map<PoolId, PoolHandle> pools;
  std::optional<PoolHandle *> last_pool;

  JobId next_job;
  PoolId next_pool;

public:
  CoordExecutor(size_t jobs_per_pool, store::FileStore &store)
      : jobs_per_pool(jobs_per_pool), store(store), next_job{0}, next_pool{0} {}

  std::map<PoolId, PoolHandle> &get_pools() { return pools; }
  const std::map<PoolId, PoolHandle> &get_pools() const { return pools; }
  PoolId get_next_pool() const { return next_pool; }
  size_t get_pool_size() const { return jobs_per_pool; }

  psx::Dynamic<std::pair<JobId, pid_t>>
  spawn_job(std::string path, std::vector<std::string> args);

  PoolHandle *pool_for_job(JobId id) {
    auto iter = pools.find(id % jobs_per_pool);
    if (iter == pools.end())
      return nullptr;

    return &iter->second;
  }
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
} // namespace command::coord
} // namespace jms
