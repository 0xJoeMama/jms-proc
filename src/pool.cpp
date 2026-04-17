#include <cassert>
#include <sys/wait.h>
#include <unistd.h>

#include "async_ops.hpp"
#include "command.hpp"
#include "executor.hpp"
#include "jms_coord.hpp"
#include "pipe.hpp"

using namespace jms;

namespace {
class JobHandle {
  JobId id;
  ProcStatus status;
  psx::Process proc;

public:
  JobHandle(store::FileStore &store, JobId id, std::string path,
            std::vector<std::string> args)
      : id(id), status(ProcStatus::Active) {}
};

struct PoolExecutor {
  std::map<JobId, JobHandle> children;
  std::map<pid_t, JobHandle *> by_pid;
};

struct Submit : JobIdCommand<PoolExecutor> {
  std::string path;
  std::vector<std::string> args;

  Submit(PeekableStream &peek) : JobIdCommand<PoolExecutor>("submit", peek) {
    if (!(peek.stream() >> path))
      throw jms::error::CommandParseError("submit requires a path argument");

    std::string s;
    while (peek.stream() >> s)
      args.emplace_back(std::move(s));
  }

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    return "SUBMIT";
  }
};

struct Suspend : JobIdCommand<PoolExecutor> {
  Suspend(PeekableStream &peek) : JobIdCommand<PoolExecutor>("suspend", peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &) override {
    return "SUSPEND";
  }
};

struct Resume : JobIdCommand<PoolExecutor> {
  Resume(PeekableStream &peek) : JobIdCommand<PoolExecutor>("resume", peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &) override {
    return "RESUME";
  }
};

struct ProcCount : jms::Command<PoolExecutor> {
  ProcCount(PeekableStream &peek) : Command<PoolExecutor>("proc-count", peek) {}
};

struct ShowActive : jms::Command<PoolExecutor> {
  ShowActive(PeekableStream &peek)
      : Command<PoolExecutor>("show-active", peek) {}
};

struct ShowFinished : jms::Command<PoolExecutor> {
  ShowFinished(PeekableStream &peek)
      : Command<PoolExecutor>("show-finished", peek) {}
};
} // namespace

int jms::pool_main(psx::DuplexConnection &conn) {
  auto [r, w] = std::move(conn);

  PoolExecutor ex;

  while (1) {
    auto cmdstr = psx::await(ReadString(r));

    try {
      auto cmd =
          jms::parse_command<PoolExecutor, Submit, ProcCount, Resume, Suspend>(
              std::move(cmdstr));

      psx::await(cmd->execute(ex).then(
          [&w = w](auto &&res) { return WriteString(std::move(res), w); }));
    } catch (const error::CommandParseError &e) {
      psx::await(WriteString(e.what(), w));
    } catch (const error::CommandExecutionError &e) {
      psx::await(WriteString(e.what(), w));
    }
  }
}
