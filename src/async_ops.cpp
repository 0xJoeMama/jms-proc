#include <limits>
#include <sstream>

#include "async_ops.hpp"
#include "except.hpp"

using namespace psx;
using namespace jms;

PollStatus<WaitReadCommand::Result>
WaitReadCommand::poll(const struct pollfd &) {
  if (cmd_len == 0) {
    try {
      r.read_all(cmd_len);
    } catch (const EofException &) {
      return Stop{};
    } catch (const ErrnoException &e) {
      switch (e.get_errno()) {
      case EAGAIN:
        /* fallthrough */
      case EINTR:
        return Continue{};
      default:
        return Stop{};
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

    return Stop{};
  } catch (const psx::EofException &) {
    return Stop{};
  }
}

void WaitReadCommand::make_wakers(psx::WakerCollector &collector) {
  int fd = r.get_fd();
  collector.push(pollfd{fd, POLLIN, 0});
}

psx::PollStatus<WriteString::Result> WriteString::poll(const struct pollfd &) {
  if (curr == std::numeric_limits<size_t>::max()) {
    try {
      w.write_all(s.size());
    } catch (const EofException &) {
      return Stop{};
    } catch (const ErrnoException &e) {
      switch (e.get_errno()) {
      case EAGAIN:
        /* fallthrough */
      case EINTR:
        return Continue{};
      default:
        return Stop{};
      };
    }

    curr = 0;
  }

  if (curr == s.size())
    return curr;

  char buf[_POSIX_PIPE_BUF];
  size_t stuff_left = std::min(static_cast<size_t>(_POSIX_PIPE_BUF), s.size() - curr);
  std::memcpy(buf, s.data() + curr, stuff_left);

  try {
    curr += w.write(buf, stuff_left);
    return Continue{};
  } catch (const ErrnoException &e) {
    if (e.get_errno() == EAGAIN || e.get_errno() == EINTR)
      return Continue{};

    return Stop{};
  } catch (const EofException &) {
    return Stop{};
  }
}

void WriteString::make_wakers(psx::WakerCollector &collector) {
  int fd = w.get_fd();
  collector.push(pollfd{fd, POLLOUT, 0});
}
