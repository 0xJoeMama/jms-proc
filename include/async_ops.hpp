#pragma once

#include <limits>

#include "pipe.hpp"
#include "poll.hpp"

namespace jms {
class WaitReadCommand : public psx::Async<std::string> {
  psx::ReadEnd &r;
  size_t cmd_len;
  std::string command;

public:
  WaitReadCommand(psx::ReadEnd &r) : r(r), cmd_len(0) {}

  psx::PollStatus<Result> poll(const struct pollfd &) override;
  void make_wakers(psx::WakerCollector &collector) override;
};

class WriteString : public psx::Async<size_t> {
  psx::WriteEnd w;
  size_t curr;
  std::string s;

public:
  WriteString(std::string s, psx::WriteEnd &&w)
      : w(std::move(w)), curr(std::numeric_limits<size_t>::max()),
        s(std::move(s)) {}

  psx::PollStatus<Result> poll(const struct pollfd &) override;
  void make_wakers(psx::WakerCollector &collector) override;
};
} // namespace jms
