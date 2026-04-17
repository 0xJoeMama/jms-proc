#include <array>
#include <csignal>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>

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
    int res = f(::getpid());
    std::quick_exit(res);
  } break;
  }

  // // do not block on write
  // auto w = fr.into_write(O_CLOEXEC);
  // // block until we can make sure that the other end has started reading
  // auto r = fw.into_read(O_CLOEXEC);
  //
  // if (fcntl(w.get_fd(), F_SETFL, O_NONBLOCK | O_CLOEXEC) < 0)
  //   throw ErrnoException(
  //       sutil::concat("failed to set communication channel with child ", pid,
  //                     " to non-blocking"));
  //
  // if (fcntl(r.get_fd(), F_SETFL, O_NONBLOCK | O_CLOEXEC) < 0)
  //   throw ErrnoException(
  //       sutil::concat("failed to set communication channel with child ", pid,
  //                     " to non-blocking"));
  //
  // return Process{
  //     pid,
  //     DuplexConnection(std::move(r), std::move(w)),
  // };
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

Process::~Process() noexcept {
  if (pid < 0)
    return;

  sigval_t v{};
  if (sigqueue(pid, SIGTERM, v) < 0)
    std::cerr << "WARN: failed to send termination signal to process " << pid
              << std::endl;

  try {
    wait_no_block();
  } catch (const ErrnoException &e) {
    std::cerr << "WARN: could not wait for child process " << pid << ": "
              << e.what() << std::endl;
  }
}
