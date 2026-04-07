#pragma once

#include "args.hpp"

namespace jms {
struct Help : NamedArg<FlagArg, Help> {
  static constexpr const char *name = "help";
  static constexpr const char shortname = 'h';
  static constexpr const char *desc = "Print this help message";
};

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

struct CoordConfig
    : Command<CoordConfig, Field<RunPath>, Field<JobsPerPool>, Field<Help>> {
  static constexpr const char *desc = "Job Management System Coordinator";
  static constexpr const char *signature =
      "-l <run-directory> -n <jobs-per-pool>";
  static constexpr const char *example = "-l . -n 12";
};

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
} // namespace jms
