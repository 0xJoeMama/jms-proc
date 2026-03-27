#include <cassert>
#include <iostream>
#include <unistd.h>
#include <vector>

#include "except.h"
#include "pipe.h"
#include "pool.h"
#include "signals.h"
#include "util.h"

std::ostream &operator<<(std::ostream &os, JobStatus status) {
  switch (status) {
  case JobStatus::Running:
    os << "Running";
    break;
  case JobStatus::Suspended:
    os << "Suspended";
    break;
  case JobStatus::Finished:
    os << "Finished";
    break;
  default:
    unreachable();
  };

  return os;
}

namespace {
void handle_sigchld(int sig, siginfo_t *info, void *) {
  assert(sig == SIGCHLD);

  JobPool *pool = JobPool::get_pool();
  pid_t chld = info->si_pid;

  // TODO: what if the process hasn't yet been added to the map yet it has
  // finished execution??
  auto _ = pool->get_by_pid(chld);
}
} // namespace

JobPool *JobPool::any_pool = nullptr;

JobPool::JobPool(size_t n) : n(n), jobs{} {
  jobs.reserve(n);

  struct sigaction action;

  action.sa_handler = NULL;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO | SA_INTERRUPT;
  action.sa_sigaction = handle_sigchld;

  if (sigaction(SIGCHLD, &action, nullptr) < 0)
    throw PosixException();

  any_pool = this;
}

const Proc &JobPool::start_job(std::vector<std::string> &&cmd) {
  if (this->jobs.size() == n)
    throw FullPoolException();

  if (cmd.size() == 0)
    throw NoCommandException();

  // disable SIGCHLD while in this region
  SignalLock lock{SIGCHLD};

  // this pipe is used as a synchronization primitive
  pipes::Pipe pipe;

  pid_t pid = fork();

  switch (pid) {
  case -1:
    throw PosixException();
  case 0: {
    const char *path = cmd[0].c_str();
    char **const args = new char *[cmd.size() + 1];

    for (size_t i = 0; i < cmd.size(); i++)
      args[i] = const_cast<char *>(cmd[i].c_str());

    args[cmd.size()] = nullptr;

    char ok;

    {
      pipes::ReadEnd r(std::move(pipe));
      r.read(&ok, sizeof(ok));
    }

    execvp(path, args);
    throw PosixException();
  } break;
  default: {
    pipes::WriteEnd w(std::move(pipe));

    char ok = 1;

    this->jobs.emplace(pid, Proc(pid));

    w.write(&ok, sizeof(ok));
  } break;
  };

  return this->jobs.at(pid);
}

void JobPool::wait_all() {

}

Proc::Proc(pid_t pid) : pid(pid), status(JobStatus::Running) {}
