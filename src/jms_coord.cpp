#include <fcntl.h>
#include <iostream>
#include <sys/poll.h>
#include <unistd.h>
#include <utility>

#include "async_ops.hpp"
#include "command.hpp"
#include "except.hpp"
#include "file_store.hpp"
#include "jms_coord.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include "signals.hpp"

using namespace jms;

namespace {
std::unique_ptr<jms::Command<CoordExecutor>>
parse_coord_command(std::string cmd_str) {
  using namespace jms::command::coord;
  return parse_command<CoordExecutor, Submit, ShowPools, ShowFinished,
                       ShowActive, Shutdown, Suspend, Resume, Status, StatusAll>(
      std::move(cmd_str));
}

class TerminatePool : public psx::Async<PoolId, TerminatePool> {
  CoordExecutor &ex;

public:
  TerminatePool(CoordExecutor &ex) : ex(ex) {}

  psx::PollStatus<Result> poll(const struct pollfd &) override {
    PoolId id;
    try {
      ex.shutdown_pipe().read_all(id);
    } catch (psx::EofException &) {
      return psx::Continue{};
    }

    return id;
  }

  void make_wakers(psx::WakerCollector &collector) override {
    if (ex.get_pools().size() == 0)
      return;

    collector(pollfd{ex.shutdown_pipe().get_fd(), POLLIN, 0});
  }
};

void handle_connection(CoordExecutor &ex, psx::DuplexConnection conn) {
  auto [r, w] = std::move(conn);
  for (;;) {
    try {
      // read a string from the duplex conn
      auto cmd_async = ReadString(r).then([&, &w = w](std::string &&cmd_str) {
        try {
          // parse that string as a coordinator command
          auto cmd = parse_coord_command(std::move(cmd_str));

          // by blocking here, we guarantee that command are
          // executed atomically. That is only one command
          // may run from time to time and until it
          // completes, no other command may execute
          auto ex_res = psx::await(cmd->execute(ex));

          // write the output to the duplex conn
          return WriteString(ex_res, w);
        } catch (const jms::error::CommandParseError &e) {
          return WriteString(e.what(), w);
        } catch (const jms::error::CommandExecutionError &e) {
          return WriteString(e.what(), w);
        }
      });

      auto result =
          psx::await(psx::select(std::move(cmd_async), TerminatePool(ex)));

      psx::select_visit(
          Match{[&](PoolId pool_id) { return ex.terminate_pool(pool_id); },
                [](auto) {}},
          std::move(result));
    } catch (const psx::EofException &e) {
      std::cout << "Client disconnected: " << e.what() << std::endl;
      break;
    } catch (const psx::ErrnoException &e) {
      std::cerr << "ERROR: could not handle client: " << e.what() << std::endl;
      break;
    }
  }
}

void run(CoordConfig &cfg) {
  store::FileStore store(cfg.get<RunPath>());
  CoordExecutor ex(cfg.get<JobsPerPool>(), store);

  try {
    for (;;) {
      auto jms_in = store.create_fifo(config::JMS_IN);
      auto jms_out = store.create_fifo(config::JMS_OUT);

      auto result = psx::await(
          psx::select(EstablishConnection(jms_in, jms_out), TerminatePool(ex)));

      psx::select_visit(
          Match{[&](psx::DuplexConnection conn) {
                  return handle_connection(ex, std::move(conn));
                },
                [&](PoolId pool_id) { return ex.terminate_pool(pool_id); }},
          std::move(result));
    }
  } catch (command::coord::ShutdownException &) {
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
