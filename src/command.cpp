#include <array>
#include <functional>

#include "command.hpp"

using namespace jms;

using CommandParser =
    std::function<std::unique_ptr<jms::Command>(PeekableStream &)>;

template <typename T> struct MakeParser : CommandParser {
  std::unique_ptr<jms::Command> operator()(PeekableStream &peek) const {
    return std::make_unique<T>(T(peek));
  }
};

const std::array<CommandParser, 9> PARSERS{
    MakeParser<Submit>(),    MakeParser<StatusAll>(),
    MakeParser<Status>(),    MakeParser<ShowActive>(),
    MakeParser<ShowPools>(), MakeParser<ShowFinished>(),
    MakeParser<Suspend>(),   MakeParser<Resume>(),
    MakeParser<Shutdown>(),
};

std::unique_ptr<jms::Command> jms::parse_command(std::string &&cmd) {
  std::istringstream is(std::move(cmd));

  size_t iter = 0;
  for (const CommandParser &parser : PARSERS) {
    try {
      std::cout << "Pass " << ++iter << std::endl;
      PeekableStream peek(is);
      return parser(peek);
    } catch (const jms::error::NextCommand &) {
    }
  }

  throw jms::error::UnknownCommand(is.str());
}
