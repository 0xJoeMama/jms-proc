#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/poll.h>

#include "args.hpp"
#include "config.hpp"
#include "except.hpp"
#include "jms_coord.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include "util.hpp"

class WaitForCommand : public psx::Async<std::string> {
  psx::ReadEnd &jms_in;
  size_t cmd_len;
  std::string command;

public:
  WaitForCommand(psx::ReadEnd &jms_in) : jms_in(jms_in), cmd_len(0) {}

  psx::PollStatus poll(short) override {
    if (cmd_len == 0) {
      try {
        jms_in.read_all(cmd_len);
      } catch (const psx::EofException &) {
        return psx::PollStatus::Stop;
      } catch (const psx::ErrnoException &e) {
        if (e.get_errno() == EAGAIN)
          return psx::PollStatus::Continue;

        return psx::PollStatus::Stop;
      }
    }

    if (command.size() == cmd_len)
      return psx::PollStatus::Done;

    std::ostringstream os{};

    try {
      jms_in.read_to_ostream(os, cmd_len - command.size());
      command.append(os.str());

      return psx::PollStatus::Done;
    } catch (const psx::ErrnoException &e) {
      if (e.get_errno() == EAGAIN) {
        command.append(os.str());
        return psx::PollStatus::Continue;
      }

      return psx::PollStatus::Stop;
    } catch (const psx::EofException &) {
      return psx::PollStatus::Stop;
    }
  }

  short get_events() const override { return POLLIN; }
  int get_fd() const override { return jms_in.get_fd(); }

  std::string &result() override { return command; }
};

using namespace jms;

Coord::Coord(CoordConfig &&cfg)
    : cfg(std::move(cfg)), store(this->cfg.get<RunPath>()),
      jms_in(
          store.create_fifo(config::JMS_IN).into_read(O_CLOEXEC | O_NONBLOCK)),
      jms_out(store.create_fifo(config::JMS_OUT)) {}

std::string Coord::execute(std::string &&command) {
  // TODO: make this actually execute the command a respond
  std::cout << "Console sent " << command << std::endl;
  return command;
}

void Coord::run() {
  // 0. It needs to wait on input from jms_in, as long as that pipe is open
  // and shutdown/SIGTERM has not been sent
  //    (we need a non-blocking fd that will signal when a client connection
  //    joins)
  // 1. It needs to read sentences from jms_in and return results to jms_out
  //    (reading of sentences can happen not-atomically, and so we need a way
  //    to keep state, while allowing for multiple consoles).
  // 2. It needs to read and write to children processes
  //    (this is also true here)
  // 3. It needs to wait on pool processes to exit
  //    (we need a signal handler for SIGCHLD that writes to an fd that we can
  //    handle synchronously)
  for (;;) {
    psx::Poll<std::string> input;
    // TODO: perhaps don't move the async into the poller to allow for select as
    // TODO: well as poll
    input.subscribe(WaitForCommand(this->jms_in));

    while (input.should_continue())
      input.run_to_completion();

    auto cmds = input.finish();
    if (cmds.empty())
      continue;

    psx::WriteEnd res_write = jms_out.into_write(0);
    std::string response = this->execute(std::move(cmds.back()));
    std::istringstream is(response);
    res_write.write_all(response.size());
    res_write.write_from_istream(is, response.size());
  }
}

int main(int, const char **argv) {
  CoordConfig config{};

  try {
    Argv args(argv + 1);
    config.parse(args);

    if (config.get<jms::Help>()) {
      config.print_usage(argv[0], std::cout);
      return 0;
    }

    Coord coord(std::move(config));
    coord.run();

    return 0;
  } catch (const error::ParsingException &e) {
    if (config.get<jms::Help>()) {
      config.print_usage(argv[0], std::cout);
      return 0;
    }

    std::cerr << "ERROR: failed to parse command line arguments: " << e.what()
              << "\n";

    config.print_usage(argv[0], std::cerr);

    return 1;
  } catch (const psx::PosixException &pe) {
    std::cerr << "ERROR: " << pe.what() << std::endl;
    return 1;
  }
}
