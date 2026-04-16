#include <fcntl.h>
#include <iostream>
#include <sys/poll.h>
#include <unistd.h>
#include <utility>

#include "async_ops.hpp"
#include "command.hpp"
#include "file_store.hpp"
#include "jms_coord.hpp"
#include "poll.hpp"
#include "signals.hpp"

using namespace jms;

namespace {
std::unique_ptr<jms::Command<CoordExecutor>>
parse_coord_command(std::string cmd_str) {
  using namespace jms::command::coord;
  return parse_command<CoordExecutor, Submit, ShowPools, ShowFinished,
                       ShowActive, Shutdown, Suspend, Resume>(
      std::move(cmd_str));
}

void run(CoordConfig &cfg) {
  store::FileStore store(cfg.get<RunPath>());
  CoordExecutor ex(cfg.get<JobsPerPool>(), store);

  for (;;) {
    auto jms_in = store.create_fifo(config::JMS_IN);
    auto jms_out = store.create_fifo(config::JMS_OUT);

    auto [r, w] = psx::await(EstablishConnection(jms_in, jms_out));

    for (;;) {
      try {
        // read a string from the duplex conn
        psx::await(ReadString(r).then([&, &w = w](std::string &&cmd_str) {
          try {
            // parse that string as a coordinator command
            auto cmd = parse_coord_command(std::move(cmd_str));

            // by blocking here, we guarantee that command are
            // executed atomically. That is only one command
            // may run from time to time and until it
            // completes, no other command may execute execute
            // that command, which returns an Dynamic<string>
            // block on the execution of that async
            auto ex_res = psx::await(cmd->execute(ex));

            // write the output to the duplex conn
            return WriteString(ex_res, w);
          } catch (const jms::error::CommandParseError &e) {
            return WriteString(e.what(), w);
          } catch (const jms::error::CommandExecutionError &e) {
            return WriteString(e.what(), w);
          }
        }));
      } catch (const psx::EofException &e) {
        std::cout << "Client disconnected: " << e.what() << std::endl;
        break;
      } catch (const psx::ErrnoException &e) {
        std::cerr << "ERROR: could not handle client: " << e.what()
                  << std::endl;
        break;
      }
    }
  }
}
} // namespace

int main(int, const char **argv) {
  CoordConfig config{};

  try {
    Argv args(argv + 1);
    config.parse(args);

    if (config.get<Help>()) {
      config.print_usage(argv[0], std::cout);
      return 0;
    }

    signals::block_unwanted();
    run(config);

    return 0;
  } catch (const ::error::ParsingException &e) {
    if (config.get<Help>()) {
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
