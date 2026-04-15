#pragma once

#include <cassert>

#define unreachable __builtin_unreachable
#define todo(s) (assert(0 && s && "not implemented yet"))

namespace config {
  constexpr int OPEN_FDS_ON_WALK = 4;
  constexpr const char *JMS_IN = "jms_in";
  constexpr const char *JMS_OUT = "jms_out";
}

template <typename...Ts>
struct Match : Ts... {
  using Ts::operator()...;
};

template <typename...Ts>
Match(Ts...) -> Match<Ts...>;
