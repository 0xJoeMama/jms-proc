#include <args.hpp>
#include <iostream>

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

struct JmsCoord : Command<JmsCoord, Field<RunPath>, Field<JobsPerPool>> {
  static constexpr const char *desc = "Job Management System Coordinator";
  static constexpr const char *signature = "-l <run-directory> -n <jobs-per-pool>";
  static constexpr const char *example = "-l . -n 12";
};

int main(int argc, const char **argv) {
  (void) argc;
  JmsCoord coords{};

  try {
    Argv args(argv + 1);
    coords.parse(args);

    auto s = coords.get<RunPath>();
    std::cout << "Running directory is " << s << std::endl;

    return 0;
  } catch (const error::ParsingException &e) {
    std::cerr << "ERROR: failed to parse command line arguments: " << e.what() << "\n";
    coords.print_usage(argv[0], std::cerr);

    return 1;
  }
}
