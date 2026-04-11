#include <charconv>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <sys/poll.h>
#include <unistd.h>
#include <utility>

#include "args.hpp"
#include "async_ops.hpp"
#include "command.hpp"
#include "config.hpp"
#include "except.hpp"
#include "jms_coord.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include "pool.hpp"
#include "util.hpp"

using namespace jms;

Coord::Coord(CoordConfig &&cfg)
    : cfg(std::move(cfg)), store(this->cfg.get<RunPath>()),
      jms_in(
          store.create_fifo(config::JMS_IN).into_read(O_CLOEXEC | O_NONBLOCK)),
      jms_out(store.create_fifo(config::JMS_OUT)), pools{},
      last_pool(std::nullopt), next_pool{0}, next_job{0} {}

std::string Coord::execute(std::string &&command) {
  try {
    auto cmd = jms::parse_command(std::move(command));
    return cmd->execute(*this);
  } catch (const jms::error::CommandParseError &e) {
    return e.what();
  } catch (const jms::error::CommandExecutionError &e) {
    return e.what();
  }
}

PoolHandle &Coord::new_pool() {
  // one path of this runs on the child only (therefore is a noreturn)
  // the other on the parent
  pid_t ret;
  PoolId pool_id = next_pool++;

  char buf[21];

  auto res = std::to_chars(buf, buf + sizeof(buf), pool_id, 10);
  *res.ptr = 0;

  store::FileStore pool_store = store.substore("pools").substore(buf);
  psx::Fifo in = pool_store.create_fifo("pool_in");
  psx::Fifo out = pool_store.create_fifo("pool_out");

  switch ((ret = fork())) {
  case -1:
    throw psx::ErrnoException("could not create pool process");
  case 0: {
    psx::ReadEnd r = in.into_read(0);
    psx::WriteEnd w = out.into_write(0);
    int ret;
    try {
      ret = pool::pool_main(cfg.get<jms::JobsPerPool>(), std::move(r),
                            std::move(w));
    } catch (const std::exception &e) {
      std::cerr << "ERROR: " << e.what() << std::endl;
      ret = 1;
    }

    exit(ret);
  }
  default: {
    pools.try_emplace(pools.end(), pool_id, pool_id, ret, out.into_read(),
                      in.into_write());
    auto &res = pools.at(pool_id);
    last_pool.emplace(&res);
    return res;
  };
  };
}

void Coord::run() {
  for (;;) {
    psx::Then<WaitReadCommand, WriteString> async(
        WaitReadCommand(jms_in), [this](std::string &&cmd) {
          psx::WriteEnd w = jms_out.into_write();
          try {
            auto res = parse_command(std::move(cmd));
            return WriteString(res->execute(*this), std::move(w));
          } catch (const error::CommandParseError &e) {
            return WriteString(e.what(), std::move(w));
          } catch (const error::CommandExecutionError &e) {
            return WriteString(e.what(), std::move(w));
          }
        });

    psx::run_to_completion(std::move(async));
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
  } catch (const ::error::ParsingException &e) {
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
