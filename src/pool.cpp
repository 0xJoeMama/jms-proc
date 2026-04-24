#include <cassert>
#include <chrono>
#include <ctime>
#include <sys/wait.h>
#include <unistd.h>

#include "async_ops.hpp"
#include "command.hpp"
#include "except.hpp"
#include "executor.hpp"
#include "file_store.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include "signals.hpp"

using namespace jms;

namespace {
std::ostream &operator<<(std::ostream &os, ProcStatus status) {
  switch (status) {
  case ProcStatus::Finished:
    return os << "Finished";
  case ProcStatus::Active:
    return os << "Active";
  case ProcStatus::Suspended:
    return os << "Suspended";
  default:
    todo("unreachable");
  };
}

namespace chr = std::chrono;

class JobHandle {
  JobId job_id;
  ProcStatus status;
  psx::Process proc;
  chr::time_point<chr::system_clock> last_start;

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
  JobHandle(JobId id, const store::FileStore &store, std::string path,
            std::vector<std::string> args)
      : job_id(id), proc([&](auto pid) {
          // first set-up file descriptors
          store::FileStore root = store.substore(make_output_dir(id, pid));

          if (chdir(root.get_root().c_str()) < 0)
            throw psx::ErrnoException(
                sutil::concat("failed to chdir into", root.get_root()));

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
          std::cerr << "failed to execute program " << path
                    << " with arguments " << sutil::concat_iter(args)
                    << std::endl;

          return 1;
        }),
        last_start(chr::system_clock::now()) {
    status = ProcStatus::Active;
  }

  psx::Process &get_proc() { return proc; }
  JobId get_job_id() const { return job_id; }
  ProcStatus get_status() const { return status; }
  void set_status(ProcStatus status) {
    this->status = status;

    if (status == jms::ProcStatus::Active)
      last_start = chr::system_clock::now();
  }

  const auto &get_last_start() const { return last_start; }
};

std::ostream &operator<<(std::ostream &os, const JobHandle &job) {
  os << "JobID " << job.get_job_id() << " Status: " << job.get_status();

  if (job.get_status() == jms::ProcStatus::Active) {
    auto now = chr::system_clock::now();
    auto diff = chr::duration_cast<chr::seconds>(now - job.get_last_start());
    os << " (running for " << diff.count() << " seconds)";
  }

  return os;
}

class PoolExecutor {
  // PoolId id;
  const store::FileStore &root_store;
  size_t njobs;
  std::map<JobId, JobHandle> children;
  std::map<pid_t, JobHandle *> by_pid;
  std::map<JobId, chr::time_point<chr::system_clock>> finished;

  psx::DuplexConnection conn;

  bool done;

public:
  PoolExecutor(PoolId /* id */, const store::FileStore &root_store,
               size_t njobs, psx::DuplexConnection conn)
      : /* id(id), */ root_store(root_store), njobs(njobs),
        conn(std::move(conn)), done(false) {}

  size_t total_procs() const { return children.size() + finished.size(); }

  void receive_sigterm() {
    size_t active = children.size();

    children.clear();
    done = true;

    // send response to parent (number of active processes)
    psx::await(WriteString(std::to_string(active), conn.w));
  }

  void receive_sigchld() {
    int res;

    for (siginfo_t si{}; (res = waitid(P_ALL, -1, &si,
                                       WNOHANG | WNOWAIT | WEXITED | WSTOPPED |
                                           WCONTINUED)) == 0 &&
                         si.si_signo == SIGCHLD;
         si = {}) {
      ProcStatus new_status = ProcStatus::Finished;

      switch (si.si_code) {
      case CLD_KILLED:
      case CLD_DUMPED:
      case CLD_EXITED: {
        auto iter = by_pid.extract(si.si_pid);
        auto node = children.extract(iter.mapped()->get_job_id());

        node.mapped().get_proc().wait_no_block();

        finished.insert({
            node.mapped().get_job_id(),
            node.mapped().get_last_start(),
        });
        continue;
      } break;

      case CLD_CONTINUED:
        new_status = ProcStatus::Active;
        break;
      case CLD_STOPPED:
        new_status = ProcStatus::Suspended;
        break;
      }

      auto iter = by_pid.at(si.si_pid);
      auto &handle = children.at(iter->get_job_id());
      handle.set_status(new_status);

      int res;
      siginfo_t si_inner;
      while ((res = waitid(P_PID, si.si_pid, &si_inner,
                           WNOHANG | WCONTINUED | WSTOPPED)) < 0 &&
             errno == EINTR)
        ;

      if (res < 0)
        std::cerr << "WARN: failed to wait on job: " << strerror(errno)
                  << std::endl;
    }

    if (res < 0 && errno != ECHILD)
      std::cerr << "WARN: failed to wait on updated job: " << strerror(errno)
                << std::endl;

    done = finished.size() == njobs;
  }

  std::optional<pid_t> new_job(JobId job_id, std::string path,
                               std::vector<std::string> args) {
    if (total_procs() == njobs)
      return std::nullopt;

    children.emplace(job_id, JobHandle(job_id, root_store, std::move(path),
                                       std::move(args)));
    auto &new_chld = children.at(job_id);
    pid_t pid = new_chld.get_proc().get_pid();
    by_pid.emplace(pid, &new_chld);

    return {pid};
  }

  std::map<JobId, JobHandle> &get_children() { return children; }
  const auto &get_finished() const { return finished; }
  psx::DuplexConnection &get_conn() { return conn; }

  bool should_continue() const { return !done; }
};

struct Submit : JobIdCommand<PoolExecutor> {
  std::string path;
  std::vector<std::string> args;

  Submit(PeekableStream &peek)
      : JobIdCommand<PoolExecutor>(command::pool::mnem::SUBMIT, peek) {
    if (!(peek.stream() >> path))
      throw jms::error::CommandParseError("submit requires a path argument");

    std::string s;
    while (peek.stream() >> s)
      args.emplace_back(std::move(s));
  }

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    auto pid = ex.new_job(job_id, std::move(path), std::move(args));
    if (pid)
      return psx::Completed<std::string>(std::to_string(*pid));
    else
      return psx::Completed<std::string>("pool is full");
  }
};

struct Suspend : JobIdCommand<PoolExecutor> {
  Suspend(PeekableStream &peek)
      : JobIdCommand<PoolExecutor>(command::pool::mnem::SUSPEND, peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    auto it = ex.get_children().find(job_id);

    if (it == ex.get_children().end())
      return psx::Completed<std::string>(
          "invalid job id or job already finished\n");

    it->second.get_proc().signal(SIGSTOP);

    return psx::Completed<std::string>(
        sutil::concat("Sent suspend signal to JobID", job_id, "\n"));
  }
};

struct Resume : JobIdCommand<PoolExecutor> {
  Resume(PeekableStream &peek)
      : JobIdCommand<PoolExecutor>(command::pool::mnem::RESUME, peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    auto it = ex.get_children().find(job_id);

    if (it == ex.get_children().end())
      return psx::Completed<std::string>(
          "invalid job id or job already finished\n");

    it->second.get_proc().signal(SIGCONT);
    return psx::Completed<std::string>(
        sutil::concat("Sent resume signal to JobID", job_id, "\n"));
  }
};

struct ProcCount : jms::Command<PoolExecutor> {
  ProcCount(PeekableStream &peek)
      : Command<PoolExecutor>(command::pool::mnem::PROC_COUNT, peek) {}

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    return psx::Completed<std::string>(
        std::to_string(ex.get_children().size()));
  }
};

struct ShowActive : jms::Command<PoolExecutor> {
  ShowActive(PeekableStream &peek)
      : Command<PoolExecutor>(command::pool::mnem::SHOW_ACTIVE, peek) {}

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    std::ostringstream os{};

    for (const auto &[id, handle] : ex.get_children())
      if (handle.get_status() == ProcStatus::Active)
        os << "JobID " << id << std::endl;

    return psx::Completed<std::string>(os.str());
  }
};

struct ShowFinished : jms::Command<PoolExecutor> {
  ShowFinished(PeekableStream &peek)
      : Command<PoolExecutor>(command::pool::mnem::SHOW_FINISHED, peek) {}

  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    std::ostringstream os{};

    for (const auto &[id, _] : ex.get_finished())
      os << "JobID " << id << std::endl;

    return psx::Completed<std::string>(os.str());
  }
};

struct Status : JobIdCommand<PoolExecutor> {
  Status(PeekableStream &peek)
      : JobIdCommand<PoolExecutor>(command::pool::mnem::STATUS, peek) {}
  psx::Dynamic<std::string> execute(PoolExecutor &ex) override {
    auto it = ex.get_children().find(job_id);

    std::ostringstream os{};

    if (it == ex.get_children().end()) {
      if (ex.get_finished().find(job_id) != ex.get_finished().end()) {
        os << "JobID " << job_id << " Status: " << ProcStatus::Finished
           << std::endl;
        return psx::Completed<std::string>(os.str());
      }

      return psx::Completed<std::string>("Job not yet submitted\n");
    }

    os << it->second << std::endl;
    return psx::Completed<std::string>(os.str());
  }
};

struct StatusAfter : public jms::JobIdCommand<PoolExecutor> {
  JobId start;

  explicit StatusAfter(PeekableStream &peek)
      : jms::JobIdCommand<PoolExecutor>(command::pool::mnem::STATUS_AFTER,
                                        peek) {}

  psx::Dynamic<std::string> execute(PoolExecutor &ex) {
    std::ostringstream os{};

    const auto &children = ex.get_children();
    for (auto it = children.lower_bound(job_id); it != children.cend(); it++)
      os << it->second << std::endl;

    const auto &finished = ex.get_finished();
    for (auto it = finished.lower_bound(job_id); it != finished.cend(); it++)
      os << "JobID " << it->first << " Status: Finished" << std::endl;

    return psx::Completed<std::string>(os.str());
  }
};
} // namespace

namespace jms {
int pool_main(PoolId id, size_t njobs, const store::FileStore &root_store,
              psx::DuplexConnection &conn) {
  signals::SignalScope scope;
  PoolExecutor ex(id, root_store, njobs, std::move(conn));

  auto &[r, w] = ex.get_conn();

  signals::block_unwanted();
  while (ex.should_continue()) {
    auto async = psx::select(ReadString(r), scope.on_signal<SIGTERM>(),
                             scope.on_signal<SIGCHLD>());
    psx::select_visit(
        Match{
            [&, &w = w](std::string &&cmdstr) {
              try {
                auto cmd =
                    jms::parse_command<PoolExecutor, Submit, Resume, Suspend,
                                       ProcCount, ShowActive, ShowFinished,
                                       Status, StatusAfter>(std::move(cmdstr));

                psx::await(cmd->execute(ex).then([&w = w](auto &&res) {
                  return WriteString(std::move(res), w);
                }));
              } catch (const error::CommandParseError &e) {
                psx::await(WriteString(e.what(), w));
              } catch (const error::CommandExecutionError &e) {
                psx::await(WriteString(e.what(), w));
              }
            },
            [&](signals::Signal<SIGCHLD>) { return ex.receive_sigchld(); },
            [&](signals::Signal<SIGTERM>) { return ex.receive_sigterm(); },
        },
        psx::await(std::move(async)));
  }

  return 0;
}
} // namespace jms
