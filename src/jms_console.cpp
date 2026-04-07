#include "args.hpp"
#include "config.hpp"
#include "pipe.hpp"
#include "util.hpp"

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

void send_operations(std::string_view, psx::WriteEnd &) {
  todo("send_operations");
}

void run(jms::ConsoleConfig cfg) {
  psx::WriteEnd w = psx::Fifo(cfg.get<jms::WritePath>(), true).into_write(0);
  std::string ops(cfg.get<jms::OpFile>());
  if (!ops.empty())
    send_operations(ops, w);

  std::string op{};
  while (std::getline(std::cin, op)) {
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

  if (!std::cin)
    throw std::runtime_error("failed to read user input");
}

int main(int, const char **argv) {
  jms::ConsoleConfig cfg{};

  try {
    Argv args(argv + 1);
    cfg.parse(args);

    if (cfg.get<jms::Help>()) {
      cfg.print_usage(*argv, std::cout);
      return 0;
    }

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
