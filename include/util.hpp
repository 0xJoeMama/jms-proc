#pragma once

#include <cassert>
#include <sstream>
#include <string>

#define unreachable __builtin_unreachable
#define todo(s) (assert(0 && s && "not implemented yet"))

namespace config {
constexpr int OPEN_FDS_ON_WALK = 4;
constexpr const char *JMS_IN = "jms_in";
constexpr const char *JMS_OUT = "jms_out";
constexpr const char *SHUTDOWN = "shutdown";
} // namespace config

template <typename... Ts> struct Match : Ts... {
  using Ts::operator()...;
};

template <typename... Ts> Match(Ts...) -> Match<Ts...>;

namespace sutil {
template <typename I> std::string concat_iter(const I &v) {
  std::ostringstream os{};

  for (const auto &i : v)
    os << i << " ";

  return os.str();
}

template <char delim = ' ', typename... Args>
std::string concat(const Args &...args) {
  std::ostringstream os{};
  (
      [&]() {
        if (os.tellp() != 0)
          os << delim;

        os << args;
      }(),
      ...);
  return os.str();
}
} // namespace sutil
