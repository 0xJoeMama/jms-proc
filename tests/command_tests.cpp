#include <cassert>
#include <sstream>

#include "command.hpp"

void test_basic_command() {
  std::istringstream is("shutdown");
  try {
    jms::PeekableStream s(is);
    jms::Shutdown shutdown(s);
  } catch (const jms::error::CommandParseError &) {
    assert(0 && "expected parsing to work");
  }

  is = std::istringstream("submit makis had a little lamb");

  try {
    jms::PeekableStream s(is);
    jms::Shutdown shutdown(s);
    assert(0 && "expected to fail");
  } catch (const jms::error::NextCommand &) {
  }

  try {
    jms::PeekableStream s(is);
    jms::Submit submit(s);
  } catch (const jms::error::CommandParseError &) {
    assert(0 && "expected to succeed");
  }
}

int main(void) {
  test_basic_command();
  return 0;
}
