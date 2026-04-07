#pragma once

#include "config.hpp"
#include "file_store.hpp"
#include "pipe.hpp"

namespace jms {
class Coord {
  const CoordConfig cfg;
  store::FileStore store;
  psx::ReadEnd jms_in;
  psx::Fifo jms_out;

public:
  explicit Coord(CoordConfig &&cfg);
  void run();
  std::string execute(std::string &&command);
};
} // namespace jms
