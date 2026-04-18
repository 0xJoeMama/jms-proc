#include <cassert>
#include <ctime>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

#include "async_ops.hpp"
#include "command.hpp"
#include "executor.hpp"
#include "file_store.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include "signals.hpp"

using namespace jms;

namespace {
class JobHandle {
  JobId job_id;
  ProcStatus status;
  psx::Process proc;

  static std::string make_output_dir(JobId job_id, pid_t pid) {
    std::ostringstream os{};

    std::time_t time = std::time(nullptr);
    char time_string[std::size("yyyymmdd_hhmmss")];
    std::strftime(time_string, sizeof(time_string), "%Y%m%e_%H%M%S",
                  std::localtime(&time));
    os << "outputs_" << job_id << "_" << pid << "_" << time_string;

    return os.str();
  }

public:
  JobHandle(const store::FileStore &store, JobId id, std::string path,
            std::vector<std::string> args)
      : job_id(id), proc([&](auto pid) {
          // first set-up file descriptors
          store::FileStore root = store.substore(make_output_dir(id, pid));

          if (chdir(root.get_root().c_str()) < 0)
            throw psx::ErrnoException(
                sutil::concat("failed to chdir into", root.get_root()));

          std::cerr << "Successfully chdir'ed into " << root.get_root().c_str()
                    << std::endl;
          if (close(STDOUT_FILENO) < 0)
            throw psx::ErrnoException(
                sutil::concat("failed to close stdout for process", pid));
          if (close(STDERR_FILENO) < 0)
            throw psx::ErrnoException(
                sutil::concat("failed to close stderr for process", pid));

          std::string fmt = sutil::concat<'_'>("stdout", id);
          psx::OwnedFd std_out(open(fmt.c_str(), O_WRONLY | O_CREAT, 0644));
          fmt = sutil::concat<'_'>("stderr", id);
          psx::OwnedFd std_err(open(fmt.c_str(), O_WRONLY | O_CREAT, 0644));

          // setup arguments and call into execve
          std::vector<char *> argv;

          argv.push_back(const_cast<char *>(path.c_str()));

          for (const auto &i : args)
            argv.push_back(const_cast<char *>(i.c_str()));

          argv.push_back(nullptr);

          ::execvp(path.c_str(), argv.data());
          throw psx::ErrnoException(sutil::concat("failed to execute program",
                                                  path, "with arguments",
                                                  sutil::concat_iter(args)));

          return 1;
        }) {
    status = ProcStatus::Active;
  }

  psx::Process &get_proc() { return proc; }
  JobId get_job_id() const { return job_id; }
  ProcStatus get_status() const { return status; }
};

struct PoolExecutor {
  PoolId id;
  const store::FileStore &root_store;
  size_t njobs;
  std::map<JobId, JobHandle> children;
  std::map<pid_t, JobHandle *> by_pid;
  std::set<JobId> finished;

  PoolExecutor(PoolId id, const store::FileStore &root_store, size_t njobs)
      : id(id), root_store(root_store), njobs(njobs) {}

  size_t total_procs() const { return children.size() + finished.size(); }
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
    if (ex.total_procs() == ex.njobs)
      return psx::Completed<std::string>("pool is full");

    ex.children.emplace(job_id, JobHandle(ex.root_store, job_id,
                                          std::move(path), std::move(args)));
    auto &new_chld = ex.children.at(job_id);
    pid_t pid = new_chld.get_proc().get_pid();
    ex.by_pid.emplace(pid, &new_chld);

    return psx::Completed<std::string>(std::to_string(pid));
  }
};

struct Suspend : JobIdCommand<PoolExecutor> {
  Suspend(PeekableStream &peek) : JobIdCommand<PoolExecutor>("suspend", peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    auto it = ex.children.find(job_id);

    if (it == ex.children.end())
      return psx::Completed<std::string>(
          "invalid job id or job already finished");

    it->second.get_proc().signal(SIGSTOP);

    return psx::Completed<std::string>("ok");
  }
};

struct Resume : JobIdCommand<PoolExecutor> {
  Resume(PeekableStream &peek) : JobIdCommand<PoolExecutor>("resume", peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    auto it = ex.children.find(job_id);

    if (it == ex.children.end())
      return psx::Completed<std::string>(
          "invalid job id or job already finished");

    it->second.get_proc().signal(SIGCONT);
    return psx::Completed<std::string>("ok");
  }
};

struct ProcCount : jms::Command<PoolExecutor> {
  ProcCount(PeekableStream &peek) : Command<PoolExecutor>("proc-count", peek) {}

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    return psx::Completed<std::string>(std::to_string(ex.children.size()));
  }
};

struct ShowActive : jms::Command<PoolExecutor> {
  ShowActive(PeekableStream &peek)
      : Command<PoolExecutor>("show-active", peek) {}

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    std::ostringstream os{};

    for (const auto &[id, handle] : ex.children)
      if (handle.get_status() == ProcStatus::Active)
        os << id << std::endl;

    return psx::Completed<std::string>(os.str());
  }
};

struct ShowFinished : jms::Command<PoolExecutor> {
  ShowFinished(PeekableStream &peek)
      : Command<PoolExecutor>("show-finished", peek) {}

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    std::ostringstream os{};

    for (const auto &id : ex.finished)
      os << id << std::endl;

    return psx::Completed<std::string>(os.str());
  }
};

struct Status : JobIdCommand<PoolExecutor> {
  Status(PeekableStream &peek) : JobIdCommand<PoolExecutor>("status", peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    auto it = ex.children.find(job_id);

    if (it == ex.children.end()) {
      if (ex.finished.find(job_id) != ex.finished.end())
        return psx::Completed<std::string>("Finished");

      return psx::Completed<std::string>(
          "invalid job id or job already finished");
    }

    switch (it->second.get_status()) {
    case ProcStatus::Active:
      return psx::Completed<std::string>("Active");
    case ProcStatus::Suspended:
      return psx::Completed<std::string>("Suspended");
    default:
      throw std::runtime_error("invalid process status in children map");
    }
  }
};
} // namespace

namespace jms {
int pool_main(PoolId id, size_t njobs, const store::FileStore &root_store,
              psx::DuplexConnection &conn) {
  auto [r, w] = std::move(conn);

  PoolExecutor ex(id, root_store, njobs);

  bool finish = false;
  while (!finish) {
    auto async = psx::select(ReadString(r), signals::OnSignal<SIGTERM>(),
                             signals::OnSignal<SIGCHLD>());
    psx::select_visit(
        Match{
            [&, &w = w](std::string &&cmdstr) {
              try {
                auto cmd =
                    jms::parse_command<PoolExecutor, Submit, Resume, Suspend,
                                       ProcCount, ShowActive, ShowFinished,
                                       Status>(std::move(cmdstr));

                psx::await(cmd->execute(ex).then([&w = w](auto &&res) {
                  return WriteString(std::move(res), w);
                }));
              } catch (const error::CommandParseError &e) {
                psx::await(WriteString(e.what(), w));
              } catch (const error::CommandExecutionError &e) {
                psx::await(WriteString(e.what(), w));
              }
            },
            [&](signals::Signal<SIGTERM>) {
              std::cerr << "INFO: got SIGTERM, shutting down..." << std::endl;
              ex.children.clear();
              finish = true;
            },
            [&](signals::Signal<SIGCHLD>) {
              siginfo_t si;
              while (waitid(P_ALL, -1, &si, WNOHANG | WNOWAIT | WEXITED) >= 0 &&
                     si.si_signo == SIGCHLD) {
                auto iter = ex.by_pid.extract(si.si_pid);
                auto node = ex.children.extract(iter.mapped()->get_job_id());

                auto exit = node.mapped().get_proc().wait_no_block();
                std::cout << "INFO: job with id " << node.mapped().get_job_id()
                          << " and PID " << si.si_pid
                          << " had exited with exit code " << WEXITSTATUS(*exit)
                          << std::endl;

                ex.finished.insert(node.mapped().get_job_id());
              }

              // TODO: handle WCONTINUED and WSTOPPED here as well
            },
        },
        psx::await(std::move(async)));
  }

  return 0;
}
} // namespace jms
