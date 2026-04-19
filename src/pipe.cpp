#include <array>
#include <csignal>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <variant>

#include "except.hpp"
#include "pipe.hpp"
#include "util.hpp"

using namespace psx;

ReadEnd::ReadEnd(OwnedFd &&fd) : fd(std::move(fd)) {}

size_t ReadEnd::read(void *buf, size_t n) {
  ssize_t res = ::read(fd.get_fd(), buf, n);

  if (res < 0)
    throw ErrnoException("failed to read from pipe");
  else if (res == 0)
    throw EofException();

  return res;
}

WriteEnd::WriteEnd(OwnedFd &&fd) : fd(std::move(fd)) {}

size_t WriteEnd::write(const void *buf, size_t n) {
  ssize_t res = ::write(fd.get_fd(), const_cast<void *>(buf), n);

  if (res < 0)
    throw ErrnoException("failed to write to pipe");
  else if (res == 0)
    throw EofException();

  return res;
}

namespace {
std::array<OwnedFd, 2> open_pipe(int flags) {
  int fds[2];
  if (pipe2(fds, flags) < 0)
    throw ErrnoException("failed to open pipe");

  return {OwnedFd(fds[0]), OwnedFd(fds[1])};
}
} // namespace

Pipe::Pipe(std::array<OwnedFd, 2> fds)
    : read(std::move(fds[0])), write(std::move(fds[1])) {}

Pipe::Pipe(int flags) : Pipe(open_pipe(flags)) {}

ReadEnd Pipe::into_read() {
  auto _ = std::move(write);
  return std::move(read);
}

WriteEnd Pipe::into_write() {
  auto _ = std::move(read);
  return std::move(write);
}

WriteEnd &Pipe::write_end() { return write; }

ReadEnd &Pipe::read_end() { return read; }
namespace {
void mkfifo(const std::string &path, bool expect_there) {
  while (::mkfifo(path.c_str(), 0644) < 0) {
    if (expect_there && errno == EEXIST)
      return;

    if (errno == EEXIST) {
      unlink(path.c_str());
    } else {
      std::ostringstream os{"could not create FIFO at "};
      os << path;

      throw ErrnoException(os.str());
    }
  }
}

OwnedFd open_fifo(const std::string &path, int flags) {
  for (;;) {
    int fd = open(path.c_str(), flags);
    if (fd < 0) {
      if (errno == EINTR || errno == ENOENT)
        continue;

      std::ostringstream os{};
      os << "could not open FIFO at " << path;
      throw ErrnoException(os.str());
    }

    OwnedFd ofd(fd);
    struct stat st;
    if (fstat(ofd.get_fd(), &st) < 0)
      throw ErrnoException("could not stat fifo");

    if (!(st.st_mode & S_IFIFO)) {
      std::ostringstream os{};
      os << "invalid node type at " << path << ": expected S_IFIFO(" << S_IFIFO
         << ") but got " << st.st_mode << "instead";

      throw FifoError(os.str());
    }

    return ofd;
  }
}
} // namespace

Fifo::Fifo(std::string_view path, bool expect_there) : path(path) {
  mkfifo(this->path, expect_there);
}

ReadEnd Fifo::into_read(int flags) {
  return open_fifo(this->path, flags | O_RDONLY);
}

WriteEnd Fifo::into_write(int flags) {
  return open_fifo(this->path, flags | O_WRONLY);
}

DuplexConnection::DuplexConnection(psx::ReadEnd r, psx::WriteEnd w)
    : r(std::move(r)), w(std::move(w)) {}

Process::Process(std::function<int(pid_t)> f) : pid(-1) {
  switch ((pid = fork())) {
  case -1:
    throw ErrnoException("failed to create child process");
  case 0: {
    int res;
    try {
      res = f(::getpid());
    } catch (const std::exception &e) {
      std::cerr << "ERROR: uncaught exception of type " << typeid(e).name()
                << " was thrown: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "ERROR: spurious uncaught exception was thrown" << std::endl;
      res = 1;
    }

    std::quick_exit(res);
  } break;
  }
}

Process::Process(Process &&other) noexcept : pid(other.pid) { other.pid = -1; }

int Process::wait() {
  int res;
  if (waitpid(pid, &res, 0) < 0) {
    std::ostringstream os{};
    os << "could not wait for child with pid: " << pid;
    throw ErrnoException(os.str());
  }

  pid = -1;

  return res;
}

std::optional<int> Process::wait_no_block() {
  int res;
  pid_t stat;

  if ((stat = waitpid(pid, &res, WNOHANG)) < 0) {
    std::ostringstream os{};
    os << "could not wait for child with pid: " << pid;
    throw ErrnoException(os.str());
  }

  if (stat == 0)
    return std::nullopt;

  pid = -1;
  return std::make_optional(res);
}

void Process::signal(int sig, std::variant<int, void *> arg) const {
  union sigval val;

  if (std::holds_alternative<int>(arg))
    val.sival_int = std::get<int>(arg);
  else
    val.sival_ptr = std::get<void *>(arg);

  if (sigqueue(pid, sig, val) < 0)
    throw ErrnoException(
        sutil::concat("failed to deliver signal to process", pid));
}

namespace {
void ignore_act(int) {
  // no-op
}
} // namespace

Process::~Process() noexcept {
  if (pid < 0)
    return;

  try {
    // try to be kind
    signal(SIGTERM);
  } catch (const ErrnoException &e) {
    std::cerr << "WARN: failed to send termination signal to process " << pid
              << std::endl;
  }

  pid_t pid = this->pid;
  struct sigaction old;

  try {
    struct sigaction act;
    act.sa_handler = ignore_act;
    act.sa_flags = SA_INTERRUPT;

    if (sigaction(SIGALRM, &act, &old) < 0) {
      sigaction(SIGALRM, nullptr, &old);
      throw ErrnoException("failed to set wakeup alarm");
    }

    // this is here in order to interrupt the wait call in case the process we
    // are signalling is stopped and/or not responding for some reason
    // We set an alarm that will interrupt the wait call. After that we KILL
    // that process as an example to others to never mess with our timing
    alarm(1);

    int exit = wait();
    std::cerr << "INFO: process with PID " << pid
              << " has been forcibly terminated. Exit code: "
              << WEXITSTATUS(exit) << std::endl;
  } catch (const ErrnoException &e) {
    std::cerr << "WARN: " << e.what() << std::endl;
    // lets actually not be kind
    signal(SIGKILL);

    wait();
    std::cout << "INFO: process with PID " << pid
              << " has been forcibly killed." << std::endl;
  }

  if (sigaction(SIGALRM, &old, nullptr) < 0)
    std::cerr << "WARN: failed to reset SIGALRM handler" << std::endl;
}
