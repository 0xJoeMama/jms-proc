#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "except.h"
#include "pipe.h"
#include "pool.h"
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
void handle_sigchld(int, siginfo_t *, void *) {
  JobPool *pool = JobPool::get_pool();
  pool->recv_sigchld();
}
} // namespace

JobPool *JobPool::any_pool = nullptr;

JobPool::JobPool(size_t n) : n(n), jobs{} {
  jobs.reserve(n);

  struct sigaction action;

  action.sa_handler = NULL;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO | SA_RESTART | SA_NOCLDSTOP | SA_NODEFER;
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

    {
      pipes::ReadEnd r(std::move(pipe));

      char ok;
      r.read(&ok, sizeof(ok));
    }

    execvp(path, args);
    throw PosixException();
  } break;
  default: {
    this->jobs.emplace(pid, Proc(pid));

    pipes::WriteEnd w(std::move(pipe));
    char ok = 1;
    w.write(&ok, sizeof(ok));
    std::cout << "Just placed " << pid << std::endl;
  } break;
  };

  return this->jobs.at(pid);
}

void JobPool::recv_sigchld() {
  pid_t pid;
  int code;

  // TODO: is there a chance that a deadlock happens here?
  // For example: finished pipe is full, and we need some data to be removed in order to write to it
  // At the same time, main thread could be waiting for data to be written to the pipe
  // Thus main is waiting on sigchld and sigchld is waiting on main -> deadlock...

  // collect all children that have exited and let the main thread know they are
  // done
  while ((pid = waitpid(-1, &code, WNOHANG)) > 0)
    // TODO: handle errors???
    this->finished.write(&pid, sizeof(pid));

  if (pid < 0) {
    auto e = PosixException();
    if (e.get_errno() == ECHILD)
      return;

    throw e;
  }
}

void JobPool::wait_all() {
  size_t completed = 0;

  while (completed < this->n) {
    pid_t pid;

    // TODO: this looks error prone af
    this->finished.read(&pid, sizeof(pid));
    this->jobs.at(pid).set_status(JobStatus::Finished);

    completed++;
  }
}

Proc::Proc(pid_t pid) : pid(pid), status(JobStatus::Running) {}
