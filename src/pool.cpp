#include <cassert>
#include <chrono>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "except.hpp"
#include "pipe.hpp"
#include "pool.hpp"
#include "util.hpp"

using namespace pool;

std::ostream &operator<<(std::ostream &os, JobStatus status) {
  switch (status) {
  case JobStatus::Active:
    os << "Active";
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

JobPool::JobPool(size_t n) : n(n), jobs{} {
  sigset_t blocked;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGCHLD);
  sigprocmask(SIG_BLOCK, &blocked, nullptr);
}

const Job &JobPool::start_job(std::vector<std::string> &&cmd) {
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
    this->jobs.emplace(pid, Job(pid));

    pipes::WriteEnd w(std::move(pipe));
    char ok = 1;
    w.write(&ok, sizeof(ok));
  } break;
  };

  return this->jobs.at(pid);
}

void JobPool::wait_all() {
  size_t completed = 0;

  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGCHLD);

  while (completed < this->n) {
    int recvd;
    sigwait(&sigs, &recvd);

    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      this->jobs.at(pid).set_status(JobStatus::Finished);
      completed++;
    }

    if (pid < 0 && errno != ECHILD)
      throw PosixException();
  }
}

Job::Job(pid_t pid)
    : pid(pid), status(JobStatus::Active),
      last_start(chr::steady_clock::now()) {}
