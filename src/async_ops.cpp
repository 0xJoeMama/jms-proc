#include <limits>

#include "async_ops.hpp"
#include "except.hpp"
#include "pipe.hpp"

using namespace psx;
using namespace jms;

DuplexConnection::DuplexConnection(psx::ReadEnd r, psx::WriteEnd w)
    : r(std::move(r)), w(std::move(w)) {}

DuplexConnection::EstablishConnection::EstablishConnection(psx::Fifo &r,
                                                           psx::Fifo &w)
    : r(r.into_read(O_NONBLOCK | O_CLOEXEC)), w(w) {}

psx::PollStatus<DuplexConnection>
DuplexConnection::EstablishConnection::poll(const struct pollfd &) {
  return DuplexConnection{
      std::move(r),
      w.into_write(O_NONBLOCK | O_CLOEXEC),
  };
}

void DuplexConnection::EstablishConnection::make_wakers(
    psx::WakerCollector &collector) {
  collector(pollfd{
      r.get_fd(),
      POLLIN,
      0,
  });
}

PollStatus<ReadString::Result> ReadString::poll(const struct pollfd &) {
  if (cmd_len == 0) {
    try {
      r.read_all(cmd_len);
    } catch (const ErrnoException &e) {
      switch (e.get_errno()) {
      case EAGAIN:
        /* fallthrough */
      case EINTR:
        return Continue{};
      default:
        throw e;
      };
    }
  }

  if (command.size() == cmd_len)
    return std::move(command);

  std::ostringstream os{};

  try {
    r.read_to_ostream(os, cmd_len - command.size());
    command.append(os.str());

    return std::move(command);
  } catch (const ErrnoException &e) {
    if (e.get_errno() == EAGAIN) {
      command.append(os.str());
      return Continue{};
    }

    throw e;
  }
}

void ReadString::make_wakers(WakerCollector &collector) {
  int fd = r.get_fd();
  collector(pollfd{fd, POLLIN, 0});
}

PollStatus<WriteString::Result> WriteString::poll(const struct pollfd &) {
  if (curr == std::numeric_limits<size_t>::max()) {
    try {
      w.write_all(s.size());
      curr = 0;
    } catch (const ErrnoException &e) {
      switch (e.get_errno()) {
      case EAGAIN:
        /* fallthrough */
      case EINTR:
        return Continue{};
      default:
        throw e;
      };
    }
  }

  if (curr == s.size())
    return std::monostate{};

  try {
    curr += w.write(s.data() + curr, s.size() - curr);

    if (curr < s.size())
      return Continue{};
    else
      return std::monostate{};
  } catch (const ErrnoException &e) {
    if (e.get_errno() == EAGAIN || e.get_errno() == EINTR)
      return Continue{};

    throw e;
  }
}

void WriteString::make_wakers(psx::WakerCollector &collector) {
  int fd = w.get_fd();
  collector(pollfd{fd, POLLOUT, 0});
}
