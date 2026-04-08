#include "args.hpp"
#include "config.hpp"
#include "except.hpp"
#include "fd.hpp"
#include "pipe.hpp"

#include <exception>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <unistd.h>

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
                     psx::WriteEnd &w) {
  psx::OwnedFd fd(open(path.c_str(), O_RDONLY));
  psx::ReadEnd r(std::move(fd));

  std::stringstream ss{};

  for (;;) {
    try {
      r.read_max_to_ostream(ss);
    } catch (const psx::EofException &) {
      return;
    }

    std::cout << "Sending file ops" << std::endl;

    std::string op{};
    while (std::getline(ss, op)) {
      if (op.empty())
        continue;

      std::cout << "Now sending " << op.size() << " bytes" << std::endl;
      handle_command(cfg, w, op);
    }

    if (ss.eof())
      ss << op;
    // TODO: make this able to handle input operations longer than 512 bytes
  }
}
} // namespace

void run(jms::ConsoleConfig cfg) {
  psx::WriteEnd w = psx::Fifo(cfg.get<jms::WritePath>(), true).into_write(0);
  std::string ops(cfg.get<jms::OpFile>());
  if (!ops.empty())
    send_operations(cfg, ops, w);

  std::string op{};

  std::cin.exceptions(std::ios_base::badbit);

  while (std::getline(std::cin, op))
    handle_command(cfg, w, op);

  std::cout << "Terminating..." << std::endl;
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
