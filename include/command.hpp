#pragma once

#include <memory>
#include <sstream>
#include <stdexcept>

#include "executor.hpp"

namespace jms {
namespace error {
class CommandParseError : public std::runtime_error {
public:
  CommandParseError(const std::string &sv) : std::runtime_error(sv) {}
};

class CommandExecutionError : public std::runtime_error {
public:
  CommandExecutionError(const std::string &sv) : std::runtime_error(sv) {}
};

class UnknownCommand : public CommandParseError {
public:
  UnknownCommand(const std::string &cmd)
      : CommandParseError("unknown command '" + cmd + "'") {}
};

class NextCommand {};
} // namespace error

class PeekableStream {
  using PType = std::istringstream::pos_type;
  std::istringstream &is;
  PType pos;

public:
  PeekableStream(std::istringstream &is) : is(is), pos(is.tellg()) {}
  PeekableStream(const PeekableStream &other) : is(other.is), pos(other.pos) {}

  ~PeekableStream() noexcept { is.seekg(pos, std::ios_base::beg); }

  std::istringstream &stream() { return is; }
};

template <typename Exec> class Command {
public:
  Command(std::string_view name, PeekableStream &peek) {
    std::string s;

    if (!(peek.stream() >> s))
      throw error::CommandParseError("empty command was sent");

    if (s != name)
      throw error::NextCommand();
  }

  virtual CommandAsync<std::string> execute(Exec &ex) = 0;
  virtual ~Command() {}
};

template <typename Exec> class JobIdCommand : public Command<Exec> {
protected:
  size_t job_id;

public:
  JobIdCommand(std::string name, PeekableStream &peek)
      : Command<Exec>(name, peek) {
    if (peek.stream().eof())
      throw error::CommandParseError("expected job_id argument");

    if (!(peek.stream() >> job_id))
      throw error::CommandParseError("job_ib must be a possible integer");
  }
};

template <typename Exec>
using CommandParser =
    std::function<std::unique_ptr<jms::Command<Exec>>(PeekableStream &)>;

template <typename T, typename Exec> struct MakeParser : CommandParser<Exec> {
  std::unique_ptr<jms::Command<Exec>> operator()(PeekableStream &peek) const {
    return std::make_unique<T>(T(peek));
  }
};

template <typename Parser>
std::unique_ptr<jms::Command<typename Parser::Exec>>
parse_command(std::string cmd) {
  std::istringstream is(std::move(cmd));

  for (const auto &parser : Parser::parsers) {
    try {
      PeekableStream peek(is);
      return parser(peek);
    } catch (const jms::error::NextCommand &) {
    }
  }

  throw jms::error::UnknownCommand(is.str());
}
} // namespace jms
