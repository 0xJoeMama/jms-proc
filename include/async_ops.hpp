#pragma once

#include <limits>

#include "pipe.hpp"
#include "poll.hpp"

namespace jms {
class EstablishConnection
    : public psx::Async<psx::DuplexConnection, EstablishConnection> {
  psx::ReadEnd r;
  psx::Fifo w;

public:
  EstablishConnection(psx::Fifo &r, psx::Fifo &w);
  psx::PollStatus<psx::DuplexConnection> poll(const struct pollfd &) override;
  void make_wakers(psx::WakerCollector &collector) override;
};

class ReadString : public psx::Async<std::string, ReadString> {
  psx::ReadEnd &r;

  size_t cmd_len;
  std::string command;

public:
  ReadString(psx::ReadEnd &r) : r(r), cmd_len(0) {}

  psx::PollStatus<Result> poll(const struct pollfd &) override;
  void make_wakers(psx::WakerCollector &collector) override;
};

class WriteString : public psx::Async<std::monostate, WriteString> {
  psx::WriteEnd &w;

  size_t curr;
  std::string s;

  static constexpr size_t MAX = std::numeric_limits<size_t>::max();

public:
  using Result = std::monostate;

  WriteString(std::string s, psx::WriteEnd &w)
      : w(w), curr(MAX), s(std::move(s)) {}

  psx::PollStatus<Result> poll(const struct pollfd &) override;
  void make_wakers(psx::WakerCollector &collector) override;
};

psx::Then<WriteString, ReadString> send_command(psx::DuplexConnection &conn,
                                                std::string cmd);
} // namespace jms
