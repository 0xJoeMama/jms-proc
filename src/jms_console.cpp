#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

#include "args.hpp"
#include "async_ops.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include "signals.hpp"

struct WritePath : RequiredArg<StringArg, WritePath> {
  static constexpr const char *name = "write-path";
  static constexpr const char shortname = 'w';
  static constexpr const char *desc = "Input FIFO file of the server (jms_in)";
};

struct ReadPath : RequiredArg<StringArg, ReadPath> {
  static constexpr const char *name = "read-path";
  static constexpr const char shortname = 'r';
  static constexpr const char *desc =
      "Output FIFO file of the server (jms_out)";
};

struct OpFile : NamedArg<StringArg, OpFile> {
  static constexpr const char *name = "operations";
  static constexpr const char shortname = 'o';
  static constexpr const char *desc = "File contains operations";
};

struct ConsoleConfig : Command<ConsoleConfig, Field<WritePath>, Field<ReadPath>,
                               Field<OpFile>, Field<Help>> {
  static constexpr const char *desc = "Job Management System Console";
  static constexpr const char *signature =
      "-w <jms_in> -r <jms_out> (-o <operations-file>)";
  static constexpr const char *example = "-w ./run/jms_in -r ./run/jms_out";
};

namespace {
void handle_command(psx::DuplexConnection &conn, const std::string &op) {
  psx::await(jms::WriteString(op, conn.w));

  auto response = psx::await(jms::ReadString(conn.r));
  std::cout << response << std::endl;
}

std::ostream &print_prompt(size_t &line) {
  return std::cout << line++ << ": jms_console > ";
}

void send_operations(std::string &path, psx::DuplexConnection &conn,
                     size_t &command_cnt) {
  std::cout << "Operation file " << path << " was provided. Sending..."
            << std::endl;
  std::ifstream fs(path);

  std::string op;
  while (std::getline(fs, op)) {
    if (op.empty())
      continue;

    print_prompt(command_cnt) << op << std::endl;
    handle_command(conn, op);
  }
}

void run(ConsoleConfig cfg) {
  auto r = psx::Fifo(cfg.get<ReadPath>(), true).into_read();
  auto w = psx::Fifo(cfg.get<WritePath>(), true).into_write();

  psx::DuplexConnection conn(std::move(r), std::move(w));
  std::string ops(cfg.get<OpFile>());

  size_t command_cnt = 0;
  if (!ops.empty())
    send_operations(ops, conn, command_cnt);

  std::string op{};

  std::cin.exceptions(std::ios_base::badbit);

  while (print_prompt(command_cnt), std::getline(std::cin, op))
    if (!op.empty())
      handle_command(conn, op);

  std::cout << "EOF given. Terminating..." << std::endl;
}
} // namespace

int main(int, const char **argv) {
  ConsoleConfig cfg{};

  try {
    Argv args(argv + 1);
    cfg.parse(args);

    if (cfg.get<Help>()) {
      cfg.print_usage(*argv, std::cout);
      return 0;
    }

    jms::signals::block_unwanted();
    run(std::move(cfg));
    return 0;
  } catch (const error::ParsingException &e) {
    if (cfg.get<Help>()) {
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
