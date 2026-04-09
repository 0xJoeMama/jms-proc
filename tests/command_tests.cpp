#include <cassert>
#include <sstream>

#include "command.hpp"

void test_basic_command() {
  std::istringstream is("shutdown");
  try {
    jms::Shutdown shutdown(is);
  } catch (const jms::CommandParseError &) {
    assert(0 && "expected parsing to work");
  }

  is = std::istringstream("submit makis had a little lamb");

  try {
    jms::Shutdown shutdown(is);
    assert(0 && "expected to fail");
  } catch (const jms::CommandParseError &) {
  }

  try {
    jms::Submit submit(is);
  } catch (const jms::CommandParseError &) {
    assert(0 && "expected to succeed");
  }
}

int main(void) {
  test_basic_command();
  return 0;
}
