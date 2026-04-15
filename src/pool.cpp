#include <cassert>
#include <sys/wait.h>
#include <unistd.h>

#include "async_ops.hpp"
#include "pipe.hpp"

namespace jms {
int pool_main(psx::DuplexConnection &conn) {
  auto [r, w] = std::move(conn);

  while (1) {
    psx::await(jms::ReadString(r).then(
        [&w = w](std::string &&cmd) { return jms::WriteString(cmd, w); }));
  }
}
} // namespace jms
