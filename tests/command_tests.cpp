#include <cassert>
#include <sstream>
#include <variant>

#include "command.hpp"
#include "poll.hpp"

class Shutdown : public jms::Command<std::monostate> {
public:
  Shutdown(jms::PeekableStream &peek)
      : jms::Command<std::monostate>("shutdown", peek) {}

  psx::Dynamic<std::string> execute(std::monostate &) override {
    return psx::Completed<std::string>("hello");
  }
};

class Submit : public jms::Command<std::monostate> {
  std::string path;
  std::vector<std::string> args;

public:
  explicit Submit(jms::PeekableStream &peek)
      : jms::Command<std::monostate>("submit", peek) {
    if (!(peek.stream() >> path))
      throw jms::error::CommandParseError(
          "no program path was provided to submit");

    std::string s;
    while (peek.stream() >> s)
      args.emplace_back(std::move(s));
  }

  psx::Dynamic<std::string> execute(std::monostate &) override {
    return psx::Completed<std::string>("hello");
  }
};

void test_basic_command() {
  std::istringstream is("shutdown");
  try {
    jms::PeekableStream s(is);
    Shutdown shutdown(s);
  } catch (const jms::error::CommandParseError &) {
    assert(0 && "expected parsing to work");
  }

  is = std::istringstream("submit makis had a little lamb");

  try {
    jms::PeekableStream s(is);
    Shutdown shutdown(s);
    assert(0 && "expected to fail");
  } catch (const jms::error::NextCommand &) {
  }

  try {
    jms::PeekableStream s(is);
    Submit submit(s);
  } catch (const jms::error::CommandParseError &) {
    assert(0 && "expected to succeed");
  }
}

int main(void) {
  test_basic_command();
  return 0;
}
