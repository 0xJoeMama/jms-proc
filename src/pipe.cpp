#include <array>
#include <sys/stat.h>

#include "pipe.hpp"
#include "fd.hpp"

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

WriteEnd &Pipe::write_end() {
  return write;
}

ReadEnd &Pipe::read_end() {
  return read;
}
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

