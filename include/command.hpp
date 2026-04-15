#pragma once

#include <chrono>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "util.hpp"

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

  ~PeekableStream() noexcept {
    is.seekg(pos, std::ios_base::beg);
  }

  std::istringstream &stream() { return is; }
};

class CommandExecutor {
public:
};

class Command {
public:
  Command(std::string_view name, PeekableStream &peek) {
    std::string s;

    if (!(peek.stream() >> s))
      throw error::CommandParseError("empty command was sent");

    if (s != name)
      throw error::NextCommand();
  }

  virtual std::string execute(CommandExecutor &ex) = 0;

  virtual ~Command() {}
};

class Submit : public Command {
  std::string path;
  std::vector<std::string> args;

public:
  Submit(PeekableStream &peek) : Command("submit", peek) {
    peek.stream() >> path;

    if (!peek.stream())
      throw error::CommandParseError("failed to get job path");

    std::string s;
    while (peek.stream() >> s)
      args.emplace_back(std::move(s));

    if (!peek.stream().eof())
      throw error::CommandParseError("failed to collect job arguments");
  }

  std::string execute(CommandExecutor &) override {
    todo("Submit::execute");
  }
};

class JobIdCommand : public Command {
protected:
  size_t job_id;

public:
  JobIdCommand(std::string name, PeekableStream &peek) : Command(name, peek) {
    if (peek.stream().eof())
      throw error::CommandParseError("expected job_id argument");

    
    if (!(peek.stream() >> job_id))
      throw error::CommandParseError("job_ib must be a possible integer");
  }
};

struct Status : public JobIdCommand {
  Status(PeekableStream &peek) : JobIdCommand("status", peek) {}
  
  std::string execute(CommandExecutor &) override {
    todo("Status::execute");
  }
};

namespace chr = std::chrono;

class StatusAll : public Command {
  chr::time_point<chr::steady_clock> since;

public:
  StatusAll(PeekableStream &peek) : Command("status-all", peek) {
    size_t since_secs;
    if (peek.stream() >> since_secs) {
      since = chr::steady_clock::now() - chr::seconds(since_secs);
    } else if (peek.stream().eof()) {
      since = decltype(since)();
    } else {
      throw error::CommandParseError("expected eof or last n seconds");
    }
  }

  std::string execute(CommandExecutor &) override {
    todo("StatusAll::execute");
  }
};

struct ShowActive : public Command {
  ShowActive(PeekableStream &peek) : Command("show-active", peek) {}

  std::string execute(CommandExecutor &) override {
    todo("ShowActive::execute");
  }
};

struct ShowPools : public Command {
  ShowPools(PeekableStream &peek) : Command("show-pools", peek) {}

  std::string execute(CommandExecutor &) override {
    todo("ShowPools::execute");
  }
};

struct ShowFinished : public Command {
  ShowFinished(PeekableStream &peek) : Command("show-finished", peek) {}

  std::string execute(CommandExecutor &) override {
    todo("ShowFinished::execute");
  }
};

struct Suspend : public JobIdCommand {
public:
  Suspend(PeekableStream &peek) : JobIdCommand("suspend", peek) {}

  std::string execute(CommandExecutor &) override {
    todo("Suspend::execute");
  }
};

struct Resume : public JobIdCommand {
  Resume(PeekableStream &peek) : JobIdCommand("resume", peek) {}

  std::string execute(CommandExecutor &) override {
    todo("Resume::execute");
  }
};

struct Shutdown : public Command {
  Shutdown(PeekableStream &peek) : Command("shutdown", peek) {}

  std::string execute(CommandExecutor &) override {
    todo("Shutdown::execute");
  }
};

std::unique_ptr<jms::Command> parse_command(std::string &&cmd);
} // namespace jms
