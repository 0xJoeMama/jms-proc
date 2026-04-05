#include <args.hpp>
#include <iostream>

#include "except.hpp"
#include "file_store.hpp"
#include "pipe.hpp"
#include "util.hpp"

struct RunPath : RequiredArg<StringArg, RunPath> {
  static constexpr const char *name = "run-path";
  static constexpr const char shortname = 'l';
  static constexpr const char *desc = "Name of directory for runtime files";
};

struct JobsPerPool : RequiredArg<IntArg, JobsPerPool> {
  static constexpr const char *name = "pool-jobs";
  static constexpr const char shortname = 'n';
  static constexpr const char *desc = "Number of jobs per job pool";
};

struct JmsCoordConfig
    : Command<JmsCoordConfig, Field<RunPath>, Field<JobsPerPool>> {
  static constexpr const char *desc = "Job Management System Coordinator";
  static constexpr const char *signature =
      "-l <run-directory> -n <jobs-per-pool>";
  static constexpr const char *example = "-l . -n 12";
};

void run(JmsCoordConfig cfg) {
  store::FileStore store(cfg.get<RunPath>());

  // 0. It needs to wait on input from jms_in, as long as that pipe is open and shutdown/SIGTERM has not been sent
  //    (we need a non-blocking fd that will signal when a client connection joins)
  // 1. It needs to read sentences from jms_in and return results to jms_out
  //    (reading of sentences can happen not-atomically, and so we need a way to keep state, while allowing for multiple consoles).
  // 2. It needs to read and write to children processes
  //    (this is also true here)
  // 3. It needs to wait on pool processes to exit
  //    (we need a signal handler for SIGCHLD that writes to an fd that we can handle synchronously)
}

int main(int, const char **argv) {
  JmsCoordConfig config{};

  try {
    Argv args(argv + 1);
    config.parse(args);
    run(std::move(config));

    return 0;
  } catch (const error::ParsingException &e) {
    std::cerr << "ERROR: failed to parse command line arguments: " << e.what()
              << "\n";

    config.print_usage(argv[0], std::cerr);

    return 1;
  } catch (const PosixException &pe) {
    std::cerr << "ERROR: " << pe.what() << std::endl;

    return 1;
  }
}
