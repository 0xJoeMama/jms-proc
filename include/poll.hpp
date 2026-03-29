#pragma once

#include <vector>

class Poll {
  std::vector<struct pollfd> fds;

public:
  Poll();

  void poll() const {
  }
};
