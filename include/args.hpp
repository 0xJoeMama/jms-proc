#pragma once

#include <algorithm>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace error {

/*
 * Exception thrown when parsing fails.
 */
class ParsingException : std::exception {
  std::string s;

public:
  ParsingException(std::string s) : s(std::move(s)) {}

  /* Get the exception message. */
  const char *what() const noexcept override { return s.data(); }
};

/*
 * Exception for missing arguments.
 */
class NotEnougArguments : public ParsingException {
public:
  NotEnougArguments(std::string s) : ParsingException(std::move(s)) {}
};

/*
 * Exception for incomplete records.
 */
class IncompleteRecord : public ParsingException {
public:
  IncompleteRecord(std::string s) : ParsingException(std::move(s)) {}
};

/*
 * Exception for non-parsable tokens.
 */
class NonParsable : public ParsingException {
public:
  NonParsable(std::string s) : ParsingException(std::move(s)) {}
};

/*
 * Exception for unexpected tokens.
 */
class UnexpectedToken : public ParsingException {
public:
  UnexpectedToken(std::string s) : ParsingException(std::move(s)) {}
};

/*
 * Exception for values out of range.
 */
class OutOfRange : public ParsingException {
public:
  OutOfRange(std::string s) : ParsingException(std::move(s)) {}
};

/*
 * Exception for invalid arguments.
 */
class InvalidArgument : public NonParsable {
public:
  InvalidArgument(std::string s)
      : NonParsable("invalid argument '" + std::move(s) + "'") {}
};

/*
 * Exception for missing required arguments.
 */
class RequiredArgNotGiven : public NonParsable {
public:
  RequiredArgNotGiven(std::string s) : NonParsable(std::move(s)) {}
};
} // namespace error

/*
 * Helper class for iterating over command line arguments.
 */
struct Argv {
  const char **argv;

  constexpr Argv(const char **argv) : argv{argv} {}

  inline const char *operator*() const { return *argv; }
  inline const char *operator++() { return *(argv++); }

  /* Get the current argument or throw if missing. */
  inline const char *get_current(const char *message) const {
    if (!*argv)
      throw error::NotEnougArguments(std::string("not enough arguments: ") +
                                     message);
    return *argv;
  }
};

class StringArg {
  std::string s;

public:
  using Type = std::string_view;

  inline void parse(Argv &argv) {
    s = argv.get_current("expected a string");
    ++argv;
  }

  inline Type get() const { return s; }
  static inline void print(std::ostream &os) { os << "[string]"; }
};

/*
 * Argument parser for integer values.
 */
class IntArg {
  int n;

public:
  using Type = int;

  constexpr IntArg(int n = 0) : n{n} {}

  /* Parse the integer from the argument stream. */
  inline void parse(Argv &argv) {
    try {
      size_t end = 0;
      std::string s = argv.get_current("expected an integer");

      n = std::stoi(s, &end);
      if (end != s.length())
        throw error::NonParsable(std::string("expected an integer got '") + s +
                                 "' instead");

      ++argv;
    } catch (std::invalid_argument &e) {
      throw error::NonParsable(std::string("expected an integer got '") +
                               *argv + "' instead");
    }
  }

  inline Type get() const { return n; }
  static inline void print(std::ostream &os) { os << "<n>"; }
};

/*
 * Argument parser for integers within a closed range [a, b].
 */
template <int A, int B> class RangedIntArg : public IntArg {
public:
  constexpr RangedIntArg(int n = 0) : IntArg(n) {}

  /* Parse and validate the integer range. */
  inline void parse(Argv &argv) {
    IntArg::parse(argv);

    if (get() < A || get() > B) {
      std::stringstream buf{};
      buf << "got integer " << get()
          << " which was outside of the allowed range [" << A << ", " << B
          << "]";

      throw error::OutOfRange(buf.str());
    }
  }
};

/*
 * Argument parser for positive integers.
 */
class PositiveIntArg : public RangedIntArg<1, std::numeric_limits<int>::max()> {
public:
  constexpr PositiveIntArg(int n = 1) : RangedIntArg(n) {}
};

/*
 * Argument parser for non-negative integers.
 */
class NonNegativeIntArg
    : public RangedIntArg<0, std::numeric_limits<int>::max()> {
public:
  constexpr NonNegativeIntArg(int n = 0) : RangedIntArg(n) {}
};

/*
 * Argument parser for boolean flags.
 */
class FlagArg {
  bool state;

public:
  using Type = bool;

  constexpr FlagArg(bool state = false) : state{state} {}

  inline void parse(Argv &) { state = true; }
  inline Type get() const { return state; }
  static inline void print(std::ostream &) {}
};

template <typename T> class PossibleArg {
  bool state;
  T deleg;

public:
  using Type = PossibleArg<T>;

  template <typename... Args>
  constexpr PossibleArg(Args... args) : state(false), deleg(args...) {}

  inline void parse(Argv &argv) {
    state = true;
    try {
      deleg.parse(argv);
    } catch (const error::ParsingException &) {
    }
  }

  inline Type get() const { return *this; }

  static inline void print(std::ostream &os) {
    os << "(";
    T::print(os);
    os << ")";
  }

  inline operator bool() const { return state; }
  inline operator typename T::Type() const { return deleg.get(); }
  inline typename T::Type value() const { return deleg.get(); }
};

/*
 * Wrapper for named arguments (flags starting with --).
 */
template <typename T, typename Desc> class NamedArg {
  T deleg;

public:
  using Type = typename T::Type;

  template <typename... Args>
  constexpr NamedArg(Args... args) : deleg(args...) {}

  /* Parse the name of the argument. */
  static void parse_long_name(Argv &argv) {
    if (std::strncmp(*argv, "--", 2) != 0)
      throw error::NonParsable("expected arguments to start with '--'");

    if (std::strcmp(*argv + 2, Desc::name) != 0)
      throw error::NonParsable(std::string("Invalid name. Expected '") +
                               Desc::name + "'");

    // name was found and matched so we consume if off the stream
    ++argv;
  }

  static void parse_short_name(Argv &argv) {
    std::string_view name = *argv;

    if (name.size() != 2)
      return parse_long_name(argv);

    if (name[0] != '-')
      throw error::NonParsable("expected short to start with '-'");

    if (name[1] != Desc::shortname)
      return parse_long_name(argv);

    ++argv;
  }

  template <typename C>
  static std::true_type has_shortname(decltype(&C::shortname));
  template <typename C> static std::false_type has_shortname(...);

  /* Parse the named argument. */
  inline void parse(Argv &argv) {
    if (!*argv)
      return;

    if constexpr (decltype(has_shortname<Desc>(nullptr))()) {
      parse_short_name(argv);
    } else {
      parse_long_name(argv);
    }

    try {
      deleg.parse(argv);
    } catch (const error::NonParsable &e) {
      throw error::UnexpectedToken(
          std::string("unexpected token after argument '--") + Desc::name +
          "': " + e.what());
    } catch (const error::ParsingException &ex) {
      throw error::ParsingException(
          std::string("error while parsing argument '") + Desc::name +
          "': " + ex.what());
    }
  }

  inline Type get() const { return deleg.get(); }

  static inline void print(std::ostream &os) {
    os << "\t--" << Desc::name << ' ';
    T::print(os);
    os << "\t" << Desc::desc << "\n";
  }
};

/*
 * Wrapper for required arguments.
 */
template <typename T, typename Desc>
class RequiredArg : public NamedArg<T, Desc> {
  bool parsed;

public:
  template <typename... Args>
  constexpr RequiredArg(Args... args)
      : NamedArg<T, Desc>(args...), parsed{false} {}

  /* Parse the argument and mark as parsed. */
  inline void parse(Argv &argv) {
    NamedArg<T, Desc>::parse(argv);
    parsed = true;
  }

  /* Check if the required argument was provided. */
  void done_parsing() const {
    if (!parsed)
      throw error::RequiredArgNotGiven(std::string("argument '") + Desc::name +
                                       "' is required but was not provided");
  }
};

template <typename Tag> struct Field {
  using TagType = Tag;
  Tag tag;
  template <typename... Args> constexpr Field(Args... args) : tag(args...) {}
};

template <typename Tag, typename... Fields> struct FindField;

template <typename Tag, typename First, typename... Rest>
struct FindField<Tag, First, Rest...> {
  struct Result {
    using Field = First;
  };

  using Field = typename std::conditional<
      std::is_same<Tag, typename First::TagType>::value,
      typename FindField<Tag, First, Rest...>::Result,
      FindField<Tag, Rest...>>::type::Field;
};

template <typename Tag> struct FindField<Tag> {
  static_assert(sizeof(Tag) == 0, "Failed to locate tag");
};

enum class RunResult {
  Continue,
  Break,
};

template <typename Self, bool first, template <typename> class Functor,
          typename Arg, typename First, typename... Rest>
RunResult for_each_field(Self &self, Arg arg) {
  try {
    RunResult res = Functor<First>::run(static_cast<First &>(self), arg);

    if (res == RunResult::Continue)
      return for_each_field<Self, false, Functor, Arg, Rest...>(self, arg);

    return RunResult::Break;
  } catch (const error::NonParsable &e) {
    if (first)
      throw e;

    throw error::IncompleteRecord(std::string("record was incomplete: ") +
                                  e.what());
  }
}

template <typename Self, bool first, template <typename> class Functor,
          typename Arg>
RunResult for_each_field(Self &, Arg &) {
  return RunResult::Continue;
}

template <typename... Fields> struct PrintFields;

template <typename First, typename... Rest> struct PrintFields<First, Rest...> {
  static void print(std::ostream &os) {
    First::TagType::print(os);
    PrintFields<Rest...>::print(os);
  }
};

template <> struct PrintFields<> {
  static void print(std::ostream &) {}
};

template <typename Field> struct ParseInorderFunctor {
  inline static RunResult run(Field &field, Argv &argv) {
    field.tag.parse(argv);
    return RunResult::Continue;
  }
};

template <typename Field>
inline void run_done_parsing(Field &field,
                             decltype(&Field::TagType::done_parsing)) {
  field.tag.done_parsing();
}

template <typename Field> void run_done_parsing(Field &, ...) {}

template <typename Field> struct DoneParsingFunctor {
  inline static RunResult run(Field &field, Argv &) {
    run_done_parsing<Field>(field, nullptr);
    return RunResult::Continue;
  }
};

/*
 * Argument composed of multiple fields.
 */
template <typename Self, typename... Fields>
struct RecordArg : public Fields... {
  using Type = const Self &;

  constexpr RecordArg(Fields... fields) : Fields(fields)... {}
  constexpr RecordArg() : Fields()... {}

  /* Get the value of a specific field. */
  template <typename Tag>
  typename FindField<Tag, Fields...>::Field::TagType::Type get() const {
    using Field = typename FindField<Tag, Fields...>::Field;
    return static_cast<const Field &>(*this).tag.get();
  }

  /* Parse all fields in order. */
  void parse(Argv &argv) {
    for_each_field<Self, true, ParseInorderFunctor, Argv &, Fields...>(
        static_cast<Self &>(*this), argv);
  }

  inline Type get() const { return *static_cast<const Self *>(this); }
  static inline void print(std::ostream &os) {
    PrintFields<Fields...>::print(os);
  }
};

struct X : IntArg {};
struct Y : IntArg {};
/*
 * Represents a positional argument with X and Y coordinates.
 */
struct PosArg : public RecordArg<PosArg, Field<X>, Field<Y>> {
  static inline void print(std::ostream &os) { os << "<x> <y>"; }
};

/*
 * Argument parser for a list of items.
 */
template <typename T> class ListArg {
  std::vector<T> v;

  struct Iter {
    using Vec = const std::vector<T>;
    Vec &v;

    Iter(Vec &v) : v{v} {}
    typename Vec::const_iterator begin() const { return v.cbegin(); }
    typename Vec::const_iterator end() const { return v.cend(); }
  };

public:
  using Type = Iter;

  inline Type get() const { return Iter(v); }

  /* Parse the list of items. */
  inline void parse(Argv &argv) {
    // assert the list has at least 1 element
    argv.get_current("expected an item for list");
    while (*argv) {
      try {
        T t;
        t.parse(argv);
        v.push_back(std::move(t));
      } catch (const error::NonParsable &) {
        break;
      }
    }
  }

  static inline void print(std::ostream &os) {
    T::print(os);
    os << " [";
    T::print(os);
    os << "]";
  }
};

template <typename Field> struct ChoiceParseFunctor {
  static RunResult run(Field &field, Argv &argv) {
    try {
      field.tag.parse(argv);
      return RunResult::Break;
    } catch (const error::NonParsable &e) {
      return RunResult::Continue;
    }
  }
};

/*
 * Argument allowing a choice between multiple fields.
 */
template <typename Self, typename... Fields>
struct ChoiceArg : public RecordArg<Self, Fields...> {
  constexpr ChoiceArg() : RecordArg<Self, Fields...>() {}

  /* Parse one of the possible fields. */
  void parse(Argv &argv) {
    auto res =
        for_each_field<Self, false, ChoiceParseFunctor, Argv &, Fields...>(
            static_cast<Self &>(*this), argv);
    if (res != RunResult::Break)
      throw error::InvalidArgument(*argv);
  }
};

/*
 * Top-level command argument parser.
 */
template <typename Self, typename... Fields>
struct Command : public ChoiceArg<Self, Fields...> {
  constexpr Command() : ChoiceArg<Self, Fields...>{} {}

  /* Parse command arguments. */
  void parse(Argv &argv) {
    do
      ChoiceArg<Self, Fields...>::parse(argv);
    while (*argv);

    for_each_field<Self, false, DoneParsingFunctor, Argv &, Fields...>(
        static_cast<Self &>(*this), argv);
  }

  static inline void print_usage(const char *prog, std::ostream &os) {
    os << Self::desc << "\n";
    os << "Usage:\n";
    os << "  " << prog << " " << Self::signature << "\n\n";
    os << "Options:\n";
    Self::print(os);
    os << "\nUsage example:\n  " << prog << " " << Self::example << "\n";
  }

  /* Print the usage message of the command */
  static inline void print(std::ostream &os) {
    std::stringstream buf{};
    ChoiceArg<Self, Fields...>::print(buf);

    // buf contains tab-separated lines which we need to turn into a table
    std::vector<std::vector<std::string>> table{};

    std::string s{};
    while (std::getline(buf, s, '\n')) {
      std::vector<std::string> row{};

      std::string cell{};
      std::istringstream tab_splitter{s};
      while (std::getline(tab_splitter, cell, '\t'))
        row.push_back(std::move(cell));

      table.push_back(std::move(row));
    }

    std::vector<size_t> col_widths{};
    for (const auto &row : table) {
      col_widths.resize(std::max(col_widths.size(), row.size()));

      for (size_t i = 0; i < row.size(); i++) {
        col_widths[i] = std::max(col_widths[i], row[i].size());
      }
    }

    for (const auto &row : table) {
      for (size_t i = 0; i < row.size(); i++) {
        os << std::setw(col_widths[i]) << std::left << row[i] << "  ";
      }

      os << "\n";
    }
  }
};

// Default help option that is commonly used
struct Help : NamedArg<FlagArg, Help> {
  static constexpr const char *name = "help";
  static constexpr const char shortname = 'h';
  static constexpr const char *desc = "Print this help message";
};
