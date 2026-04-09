#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

#include "args.hpp"
#include "config.hpp"
#include "signals.hpp"
#include "pipe.hpp"

namespace {
void handle_command(const jms::ConsoleConfig &cfg, psx::WriteEnd &w,
                    const std::string &op) {
  std::istringstream is(op);
  w.write_all(op.size());
  w.write_from_istream(is, op.size());

  psx::ReadEnd r = psx::Fifo(cfg.get<jms::ReadPath>(), true).into_read(0);
  size_t res_len = 0;
  r.read_all(res_len);
  std::ostringstream os{};
  r.read_to_ostream(os, res_len);
  std::string response = os.str();
  std::cout << response << std::endl;
}

void send_operations(const jms::ConsoleConfig &cfg, std::string &path,
                     psx::WriteEnd &w, size_t &command_cnt) {
  std::cout << "Operation file " << path << " was provided. Sending..." << std::endl;
  std::ifstream fs(path);

  std::string op;
  while (std::getline(fs, op)) {
    if (op.empty())
      continue;

    std::cout << command_cnt++ << ": jms_console > " << op << std::endl;
    handle_command(cfg, w, op);
  }
}

void run(jms::ConsoleConfig cfg) {
  psx::WriteEnd w = psx::Fifo(cfg.get<jms::WritePath>(), true).into_write(0);
  std::string ops(cfg.get<jms::OpFile>());

  size_t command_cnt = 0;
  if (!ops.empty())
    send_operations(cfg, ops, w, command_cnt);

  std::string op{};

  std::cin.exceptions(std::ios_base::badbit);

  while (std::cout << command_cnt++ << ": jms_console > ", std::getline(std::cin, op)) {
    if (op.empty()) 
      continue;

    handle_command(cfg, w, op);
  }

  std::cout << "EOF given. Terminating..." << std::endl;
}
} // namespace

int main(int, const char **argv) {
  jms::ConsoleConfig cfg{};

  try {
    Argv args(argv + 1);
    cfg.parse(args);

    if (cfg.get<jms::Help>()) {
      cfg.print_usage(*argv, std::cout);
      return 0;
    }

    jms::signals::block_unwanted();
    run(std::move(cfg));
    return 0;
  } catch (const error::ParsingException &e) {
    if (cfg.get<jms::Help>()) {
      cfg.print_usage(*argv, std::cout);
      return 0;
    }

    std::cerr << "ERROR: failed to parse command line: " << e.what()
              << std::endl;
    cfg.print_usage(*argv, std::cerr);
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
