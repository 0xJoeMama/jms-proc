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

class JmsCoord {
  const JmsCoordConfig config;
  const store::FileStore store;
  const pipes::ReadEnd jms_in;

public:
  JmsCoord(JmsCoordConfig &&config)
      : config(std::move(config)), store(this->config.get<RunPath>()),
        jms_in(pipes::ReadEnd(store.create_fifo(config::JMS_IN))) {}

  const store::FileStore &get_store() const { return store; }
};

int main(int, const char **argv) {
  JmsCoordConfig config{};

  try {
    Argv args(argv + 1);
    config.parse(args);
    JmsCoord coord(std::move(config));

    pipes::Fifo f2 =
        coord.get_store().substore("makis").create_fifo("fifo2");

    sleep(10);
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
