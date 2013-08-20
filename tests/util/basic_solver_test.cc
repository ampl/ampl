/*
 Basic solver tests.

 Copyright (C) 2012 AMPL Optimization LLC

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that the copyright notice and this permission notice and warranty
 disclaimer appear in supporting documentation.

 The author and AMPL Optimization LLC disclaim all warranties with
 regard to this software, including all implied warranties of
 merchantability and fitness.  In no event shall the author be liable
 for any special, indirect or consequential damages or any damages
 whatsoever resulting from loss of use, data or profits, whether in an
 action of contract, negligence or other tortious action, arising out
 of or in connection with the use or performance of this software.

 Author: Victor Zverovich
 */

#include "gtest/gtest.h"
#include "solvers/util/solver.h"
#include "tests/args.h"
#include "tests/config.h"
#include "tests/solution_handler.h"
#include "tests/util.h"

#ifdef WIN32
# define putenv _putenv
#endif

using ampl::BasicSolver;
using ampl::OptionError;
using ampl::Problem;
using ampl::Solver;
using ampl::SolverOption;
using ampl::internal::OptionHelper;

namespace {

typedef BasicSolver::OptionPtr SolverOptionPtr;

struct TestSolver : BasicSolver {
  TestSolver(const char *name = "testsolver",
      const char *long_name = 0, long date = 0)
  : BasicSolver(name, long_name, date) {}

  void set_long_name(const char *name) {
    BasicSolver::set_long_name(name);
  }

  void set_version(const char *version) {
    BasicSolver::set_version(version);
  }

  void AddOption(OptionPtr opt) {
    BasicSolver::AddOption(move(opt));
  }

  bool ParseOptions(char **argv, unsigned flags = BasicSolver::NO_OPTION_ECHO) {
    return BasicSolver::ParseOptions(argv, flags);
  }

  void Solve(Problem &) {}
};
}

TEST(SolverTest, ObjPrec) {
  double value = 12.3456789123456789;
  char buffer[64];
  sprintf(buffer, "%.*g", obj_prec(), value);
  EXPECT_EQ(buffer, str(fmt::Format("{}") << ampl::ObjPrec(value)));
}

TEST(SolverTest, Format) {
  EXPECT_EQ(
    "     This is a very long option description that should be indented and\n"
    "     wrapped.\n",
    ampl::internal::IndentAndWordWrap(
          "This is a very long option description "
          "that should be indented and wrapped.", 5));
}

TEST(SolverTest, BasicSolverCtor) {
  TestSolver s;
  EXPECT_EQ(0, s.problem().num_vars());
  EXPECT_STREQ("testsolver", s.name());
  EXPECT_STREQ("testsolver", s.long_name());
  EXPECT_STREQ("testsolver_options", s.options_var_name());
  EXPECT_STREQ("testsolver", s.version());
  EXPECT_EQ(0, s.date());
  EXPECT_EQ(0, s.flags());
  EXPECT_EQ(0, s.wantsol());
}

TEST(SolverTest, BasicSolverVirtualDtor) {
  bool destroyed = false;
  class DtorTestSolver : public BasicSolver {
   private:
    bool &destroyed_;

   public:
    DtorTestSolver(bool &destroyed)
    : BasicSolver("test", 0, 0), destroyed_(destroyed) {}
    ~DtorTestSolver() { destroyed_ = true; }
    void Solve(Problem &) {}
  };
  (DtorTestSolver(destroyed));
  EXPECT_TRUE(destroyed);
}

TEST(SolverTest, NameInUsage) {
  {
    StderrRedirect redirect("out");
    TestSolver s("solver-name", "long-solver-name");
    s.set_version("solver-version");
    s.ProcessArgs(Args("program-name"));
  }
  std::string usage = "usage: solver-name ";
  EXPECT_EQ(usage, ReadFile("out").substr(0, usage.size()));
}

TEST(SolverTest, LongName) {
  EXPECT_STREQ("solver-name", TestSolver("solver-name").long_name());
  EXPECT_STREQ("long-solver-name",
      TestSolver("solver-name", "long-solver-name").long_name());
  TestSolver s("solver-name");
  s.set_long_name("another-name");
  EXPECT_STREQ("another-name", s.long_name());
}

TEST(SolverTest, Version) {
  TestSolver s("testsolver", "Test Solver");
  EXPECT_EXIT({
    FILE *f = freopen("out", "w", stdout);
    s.ProcessArgs(Args("program-name", "-v"));
    fclose(f);
  }, ::testing::ExitedWithCode(0), "");
  fmt::Formatter format;
  format("Test Solver ({}), ASL({})\n") << sysdetails_ASL << ASLdate_ASL;
  EXPECT_EQ(format.str(), ReadFile("out"));
}

TEST(SolverTest, VersionWithDate) {
  TestSolver s("testsolver", "Test Solver", 20121227);
  EXPECT_EXIT({
    FILE *f = freopen("out", "w", stdout);
    s.ProcessArgs(Args("program-name", "-v"));
    fclose(f);
  }, ::testing::ExitedWithCode(0), "");
  fmt::Formatter format;
  format("Test Solver ({}), driver(20121227), ASL({})\n")
    << sysdetails_ASL << ASLdate_ASL;
  EXPECT_EQ(format.str(), ReadFile("out"));
}

TEST(SolverTest, SetVersion) {
  TestSolver s("testsolver", "Test Solver");
  const char *VERSION = "Solver Version 3.0";
  s.set_version(VERSION);
  EXPECT_STREQ(VERSION, s.version());
  EXPECT_EXIT({
    FILE *f = freopen("out", "w", stdout);
    s.ProcessArgs(Args("program-name", "-v"));
    fclose(f);
  }, ::testing::ExitedWithCode(0), "");
  fmt::Formatter format;
  format("{} ({}), ASL({})\n") << VERSION << sysdetails_ASL << ASLdate_ASL;
  EXPECT_EQ(format.str(), ReadFile("out"));
}

TEST(SolverTest, ErrorHandler) {
  struct TestErrorHandler : ampl::ErrorHandler {
    std::string message;

    virtual ~TestErrorHandler() {}
    void HandleError(fmt::StringRef message) {
      this->message = message;
    }
  };

  TestErrorHandler eh;
  TestSolver s("test");
  s.set_error_handler(&eh);
  EXPECT_TRUE(&eh == s.error_handler());
  s.ReportError("test message");
  EXPECT_EQ("test message", eh.message);
}

TEST(SolverTest, OutputHandler) {
  struct TestOutputHandler : ampl::OutputHandler {
    std::string output;

    virtual ~TestOutputHandler() {}
    void HandleOutput(fmt::StringRef output) {
      this->output += output;
    }
  };

  TestOutputHandler oh;
  TestSolver s("test");
  s.set_output_handler(&oh);
  EXPECT_TRUE(&oh == s.output_handler());
  s.Print("line {}\n") << 1;
  s.Print("line {}\n") << 2;
  EXPECT_EQ("line 1\nline 2\n", oh.output);
}

TEST(SolverTest, SolutionHandler) {
  TestSolutionHandler sh;
  TestSolver s("test");
  s.set_solution_handler(&sh);
  EXPECT_TRUE(&sh == s.solution_handler());
  double primal = 0, dual = 0, obj = 42;
  s.HandleSolution("test message", &primal, &dual, obj);
  EXPECT_EQ(&s, sh.solver());
  EXPECT_EQ("test message", sh.message());
  EXPECT_EQ(&primal, sh.primal());
  EXPECT_EQ(&dual, sh.dual());
  EXPECT_EQ(42.0, sh.obj_value());
}

TEST(SolverTest, ReadProblem) {
  TestSolver s("test");
  EXPECT_EQ(0, s.problem().num_vars());
  EXPECT_TRUE(s.ProcessArgs(Args("testprogram", "../data/objconst.nl")));
  EXPECT_EQ(1, s.problem().num_vars());
}

TEST(SolverTest, ReadProblemNoStub) {
  StderrRedirect redirect("out");
  TestSolver s("test");
  EXPECT_EQ(0, s.problem().num_vars());
  EXPECT_FALSE(s.ProcessArgs(Args("testprogram")));
  EXPECT_EQ(0, s.problem().num_vars());
}

TEST(SolverTest, ReadProblemError) {
  TestSolver s("test");
  EXPECT_EXIT({
    Stderr = stderr;
    s.ProcessArgs(Args("testprogram", "nonexistent"));
  }, ::testing::ExitedWithCode(1), "testprogram: can't open nonexistent.nl");
}

TEST(SolverTest, ReadingMinOrMaxWithZeroArgsFails) {
  const char *names[] = {"min", "max"};
  for (size_t i = 0, n = sizeof(names) / sizeof(*names); i < n; ++i) {
    std::string stub = str(fmt::Format("../data/{}-with-zero-args") << names[i]);
    EXPECT_EXIT({
      Stderr = stderr;
      Problem p;
      p.Read(stub);
    }, ::testing::ExitedWithCode(1),
        c_str(fmt::Format("bad line 13 of {}.nl: 0") << stub));
  }
}

TEST(SolverTest, ReportError) {
  TestSolver s("test");
  EXPECT_EXIT({
    s.ReportError("File not found: {}") << "somefile";
    exit(0);
  }, ::testing::ExitedWithCode(0), "File not found: somefile");
}

TEST(SolverTest, ProcessArgsReadsProblem) {
  TestSolver s;
  EXPECT_EQ(0, s.problem().num_vars());
  EXPECT_TRUE(s.ProcessArgs(Args("testprogram", "../data/objconst.nl")));
  EXPECT_EQ(1, s.problem().num_vars());
}

TEST(SolverTest, ProcessArgsParsesSolverOptions) {
  TestSolver s;
  EXPECT_TRUE(s.ProcessArgs(
      Args("testprogram", "../data/objconst.nl", "wantsol=5"),
      BasicSolver::NO_OPTION_ECHO));
  EXPECT_EQ(5, s.wantsol());
}

TEST(SolverTest, ProcessArgsWithouStub) {
  StderrRedirect redirect("out");
  TestSolver s;
  EXPECT_EQ(0, s.problem().num_vars());
  EXPECT_FALSE(s.ProcessArgs(Args("testprogram")));
  EXPECT_EQ(0, s.problem().num_vars());
}

TEST(SolverTest, ProcessArgsError) {
  TestSolver s;
  EXPECT_EXIT({
    Stderr = stderr;
    s.ProcessArgs(Args("testprogram", "nonexistent"));
  }, ::testing::ExitedWithCode(1), "testprogram: can't open nonexistent.nl");
}

TEST(SolverTest, SignalHandler) {
  std::signal(SIGINT, SIG_DFL);
  TestSolver s;
  EXPECT_EXIT({
    FILE *f = freopen("out", "w", stdout);
    ampl::SignalHandler sh(s);
    fmt::Print("{}") << ampl::SignalHandler::stop();
    std::fflush(stdout);
    std::raise(SIGINT);
    fmt::Print("{}") << ampl::SignalHandler::stop();
    fclose(f);
    exit(0);
  }, ::testing::ExitedWithCode(0), "");
  EXPECT_EQ("0\n<BREAK> (testsolver)\n1", ReadFile("out"));
}

TEST(SolverTest, SignalHandlerExitOnTwoSIGINTs) {
  std::signal(SIGINT, SIG_DFL);
  TestSolver s;
  EXPECT_EXIT({
    ampl::SignalHandler sh(s);
    FILE *f = freopen("out", "w", stdout);
    std::raise(SIGINT);
    std::raise(SIGINT);
    std::fclose(f); // Unreachable, but silences a warning.
  }, ::testing::ExitedWithCode(1), "");
  EXPECT_EQ("\n<BREAK> (testsolver)\n\n<BREAK> (testsolver)\n",
      ReadFile("out"));
}

// ----------------------------------------------------------------------------
// Option tests

TEST(SolverTest, SolverOption) {
  struct TestOption : SolverOption {
    bool formatted, parsed;
    TestOption(const char *name, const char *description)
    : SolverOption(name, description), formatted(false), parsed(false) {}
    TestOption(const char *name, const char *description, bool is_keyword)
    : SolverOption(name, description, is_keyword),
      formatted(false), parsed(false) {}
    void Format(fmt::Formatter &) { formatted = true; }
    void Parse(const char *&) { parsed = true; }
  };
  {
    TestOption opt("abc", "def");
    EXPECT_STREQ("abc", opt.name());
    EXPECT_STREQ("def", opt.description());
    EXPECT_FALSE(opt.is_keyword());
  }
  {
    TestOption opt("", "", true);
    EXPECT_TRUE(opt.is_keyword());
  }
  {
    TestOption opt("", "");
    EXPECT_FALSE(opt.formatted);
    EXPECT_FALSE(opt.parsed);
    SolverOption &so = opt;
    fmt::Formatter f;
    so.Format(f);
    EXPECT_TRUE(opt.formatted);
    const char *s = 0;
    so.Parse(s);
    EXPECT_TRUE(opt.parsed);
  }
}

TEST(SolverTest, IntOptionHelper) {
  fmt::Formatter f;
  OptionHelper<int>::Format(f, 42);
  EXPECT_EQ("42", str(f));
  const char *start = "123 ";
  const char *s = start;
  EXPECT_EQ(123, OptionHelper<int>::Parse(s));
  EXPECT_EQ(start + 3, s);
  EXPECT_EQ(42, OptionHelper<int>::CastArg(42));
}

TEST(SolverTest, DoubleOptionHelper) {
  fmt::Formatter f;
  OptionHelper<double>::Format(f, 4.2);
  EXPECT_EQ("4.2", str(f));
  const char *start = "1.23 ";
  const char *s = start;
  EXPECT_EQ(1.23, OptionHelper<double>::Parse(s));
  EXPECT_EQ(start + 4, s);
  EXPECT_EQ(4.2, OptionHelper<double>::CastArg(4.2));
}

TEST(SolverTest, StringOptionHelper) {
  fmt::Formatter f;
  OptionHelper<std::string>::Format(f, "abc");
  EXPECT_EQ("abc", str(f));
  const char *start = "def ";
  const char *s = start;
  EXPECT_EQ("def", OptionHelper<std::string>::Parse(s));
  EXPECT_EQ(start + 3, s);
  EXPECT_STREQ("abc", OptionHelper<std::string>::CastArg(std::string("abc")));
}

TEST(SolverTest, TypedSolverOption) {
  struct TestOption : ampl::TypedSolverOption<int> {
    int value;
    TestOption(const char *name, const char *description)
    : TypedSolverOption<int>(name, description), value(0) {}
    int GetValue() const { return value; }
    void SetValue(int value) { this->value = value; }
  };
  TestOption opt("abc", "def");
  EXPECT_STREQ("abc", opt.name());
  EXPECT_STREQ("def", opt.description());
  EXPECT_FALSE(opt.is_keyword());
  const char *start = "42";
  const char *s = start;
  opt.Parse(s);
  EXPECT_EQ(start + 2, s);
  EXPECT_EQ(42, opt.value);
  fmt::Formatter f;
  opt.Format(f);
  EXPECT_EQ("42", str(f));
}

enum Info { INFO = 0xcafe };

struct TestSolverWithOptions : Solver<TestSolverWithOptions> {
  int intopt1;
  int intopt2;
  double dblopt1;
  double dblopt2;
  std::string stropt1;
  std::string stropt2;

  int GetIntOption(const char *) const { return intopt1; }
  void SetIntOption(const char *name, int value) {
    EXPECT_STREQ("intopt1", name);
    intopt1 = value;
  }

  int GetIntOptionWithInfo(const char *, Info) const { return 0; }
  void SetIntOptionWithInfo(const char *name, int value, Info info) {
    EXPECT_STREQ("intopt2", name);
    intopt2 = value;
    EXPECT_EQ(INFO, info);
  }

  double GetDblOption(const char *) const { return dblopt1; }
  void SetDblOption(const char *name, double value) {
    EXPECT_STREQ("dblopt1", name);
    dblopt1 = value;
  }

  double GetDblOptionWithInfo(const char *, Info) const { return 0; }
  void SetDblOptionWithInfo(const char *name, double value, Info info) {
    EXPECT_STREQ("dblopt2", name);
    dblopt2 = value;
    EXPECT_EQ(INFO, info);
  }

  std::string GetStrOption(const char *) const { return stropt1; }
  void SetStrOption(const char *name, const char *value) {
    EXPECT_STREQ("stropt1", name);
    stropt1 = value;
  }

  std::string GetStrOptionWithInfo(const char *, Info) const { return ""; }
  void SetStrOptionWithInfo(const char *name, const char *value, Info info) {
    EXPECT_STREQ("stropt2", name);
    stropt2 = value;
    EXPECT_EQ(INFO, info);
  }

  TestSolverWithOptions()
  : Solver<TestSolverWithOptions>("testsolver"),
    intopt1(0), intopt2(0), dblopt1(0), dblopt2(0) {
    AddIntOption("intopt1", "Integer option 1",
        &TestSolverWithOptions::GetIntOption,
        &TestSolverWithOptions::SetIntOption);
    AddIntOption("intopt2", "Integer option 2",
        &TestSolverWithOptions::GetIntOptionWithInfo,
        &TestSolverWithOptions::SetIntOptionWithInfo, INFO);
    AddDblOption("dblopt1", "Double option 1",
        &TestSolverWithOptions::GetDblOption,
        &TestSolverWithOptions::SetDblOption);
    AddDblOption("dblopt2", "Double option 2",
        &TestSolverWithOptions::GetDblOptionWithInfo,
        &TestSolverWithOptions::SetDblOptionWithInfo, INFO);
    AddStrOption("stropt1", "Double option 1",
        &TestSolverWithOptions::GetStrOption,
        &TestSolverWithOptions::SetStrOption);
    AddStrOption("stropt2", "Double option 2",
        &TestSolverWithOptions::GetStrOptionWithInfo,
        &TestSolverWithOptions::SetStrOptionWithInfo, INFO);
  }

  bool ParseOptions(char **argv, unsigned flags = BasicSolver::NO_OPTION_ECHO) {
    return Solver<TestSolverWithOptions>::ParseOptions(argv, flags);
  }

  void Solve(Problem &) {}
};

TEST(SolverTest, AddOption) {
  struct TestOption : SolverOption {
    int value;
    TestOption() : SolverOption("testopt", "A test option."), value(0) {}

    void Format(fmt::Formatter &) {}
    void Parse(const char *&s) {
      char *end = 0;
      value = std::strtol(s, &end, 10);
      s = end;
    }
  };
  TestSolver s;
  TestOption *opt = 0;
  s.AddOption(SolverOptionPtr(opt = new TestOption()));
  EXPECT_TRUE(s.ParseOptions(Args("testopt=42"), BasicSolver::NO_OPTION_ECHO));
  EXPECT_EQ(42, opt->value);
}

TEST(SolverTest, ParseOptionsFromArgs) {
  TestSolverWithOptions s;
  EXPECT_TRUE(s.ParseOptions(Args("intopt1=5 intopt2=7")));
  EXPECT_EQ(5, s.intopt1);
  EXPECT_EQ(7, s.intopt2);
}

TEST(SolverTest, ParseOptionsFromEnvVar) {
  TestSolverWithOptions s;
  char options[] = "testsolver_options=intopt1=9 intopt2=11";
  putenv(options);
  EXPECT_TRUE(s.ParseOptions(Args(0)));
  EXPECT_EQ(9, s.intopt1);
  EXPECT_EQ(11, s.intopt2);
  char reset_options[] = "testsolver_options=";
  putenv(reset_options);
}

TEST(SolverTest, ParseOptionsNoArgs) {
  TestSolver s;
  EXPECT_TRUE(s.ParseOptions(Args(0)));
}

TEST(SolverTest, ParseOptionsSkipsWhitespace) {
  TestSolverWithOptions s;
  EXPECT_TRUE(s.ParseOptions(Args(
      " \t\r\n\vintopt1 \t\r\n\v= \t\r\n\v5"
      " \t\r\n\vintopt2 \t\r\n\v7 \t\r\n\v")));
  EXPECT_EQ(5, s.intopt1);
  EXPECT_EQ(7, s.intopt2);
}

TEST(SolverTest, ParseOptionsCaseInsensitiveName) {
  TestSolverWithOptions s;
  EXPECT_TRUE(s.ParseOptions(Args("IntOpt1=42")));
  EXPECT_EQ(42, s.intopt1);
  EXPECT_TRUE(s.ParseOptions(Args("INTOPT1=21")));
  EXPECT_EQ(21, s.intopt1);
}

TEST(SolverTest, ParseOptionsNoEqualSign) {
  TestSolverWithOptions s;
  EXPECT_TRUE(s.ParseOptions(Args("stropt1 abc")));
  EXPECT_EQ("abc", s.stropt1);
}

struct TestErrorHandler : ampl::ErrorHandler {
  std::vector<std::string> errors;

  virtual ~TestErrorHandler() {}
  void HandleError(fmt::StringRef message) {
    errors.push_back(message);
  }
};

TEST(SolverTest, UnknownOption) {
  TestSolverWithOptions s;
  TestErrorHandler handler;
  s.set_error_handler(&handler);
  EXPECT_FALSE(s.ParseOptions(Args("badopt1=3 badopt2 intopt1=42 badopt3")));
  EXPECT_EQ(3u, handler.errors.size());
  EXPECT_EQ("Unknown option \"badopt1\"", handler.errors[0]);
  EXPECT_EQ("Unknown option \"badopt2\"", handler.errors[1]);
  EXPECT_EQ("Unknown option \"badopt3\"", handler.errors[2]);
  EXPECT_EQ(42, s.intopt1);
}

TEST(SolverTest, HandleUnknownOption) {
  struct TestSolver : BasicSolver {
    std::string option_name;
    TestSolver() : BasicSolver("test", 0, 0) {}
    void Solve(Problem &) {}
    void HandleUnknownOption(const char *name) { option_name = name; }
  };
  TestSolver s;
  s.ParseOptions(Args("BadOption"));
  EXPECT_EQ("BadOption", s.option_name);
}

TEST(SolverTest, ParseOptionRecovery) {
  TestSolverWithOptions s;
  TestErrorHandler handler;
  s.set_error_handler(&handler);
  // After encountering an unknown option without "=" parsing should skip
  // everything till the next known option.
  EXPECT_FALSE(s.ParseOptions(Args("badopt1 3 badopt2=1 intopt1=42 badopt3")));
  EXPECT_EQ(2u, handler.errors.size());
  EXPECT_EQ("Unknown option \"badopt1\"", handler.errors[0]);
  EXPECT_EQ("Unknown option \"badopt3\"", handler.errors[1]);
  EXPECT_EQ(42, s.intopt1);
}

struct FormatOption : SolverOption {
  int format_count;
  FormatOption() : SolverOption("fmtopt", ""), format_count(0) {}

  void Format(fmt::Formatter &f) {
    f("1");
    ++format_count;
  }
  void Parse(const char *&) {}
};

TEST(SolverTest, FormatOption) {
  EXPECT_EXIT({
    TestSolver s;
    FILE *f = freopen("out", "w", stdout);
    s.AddOption(SolverOptionPtr(new FormatOption()));
    s.ParseOptions(Args("fmtopt=?"), 0);
    printf("---\n");
    s.ParseOptions(Args("fmtopt=?", "fmtopt=?"), 0);
    fclose(f);
    exit(0);
  }, ::testing::ExitedWithCode(0), "");
  EXPECT_EQ("fmtopt=1\n---\nfmtopt=1\nfmtopt=1\n", ReadFile("out"));
}

TEST(SolverTest, OptionNotPrintedWhenEchoOff) {
  TestSolver s;
  FormatOption *opt = 0;
  s.AddOption(SolverOptionPtr(opt = new FormatOption()));
  EXPECT_EQ(0, opt->format_count);
  s.ParseOptions(Args("fmtopt=?"));
  EXPECT_EQ(0, opt->format_count);
}

TEST(SolverTest, NoEchoWhenPrintingOption) {
  EXPECT_EXIT({
    TestSolver s;
    FILE *f = freopen("out", "w", stdout);
    s.AddOption(SolverOptionPtr(new FormatOption()));
    s.ParseOptions(Args("fmtopt=?"));
    fclose(f);
    exit(0);
  }, ::testing::ExitedWithCode(0), "");
  EXPECT_EQ("", ReadFile("out"));
}

TEST(SolverTest, QuestionMarkInOptionValue) {
  TestSolverWithOptions s;
  s.ParseOptions(Args("stropt1=?x"));
  EXPECT_EQ("?x", s.stropt1);
}

TEST(SolverTest, ErrorOnKeywordOptionValue) {
  struct KeywordOption : SolverOption {
    bool parsed;
    KeywordOption() : SolverOption("kwopt", "", true), parsed(false) {}
    void Format(fmt::Formatter &) {}
    void Parse(const char *&) { parsed = true; }
  };
  TestSolver s;
  TestErrorHandler handler;
  s.set_error_handler(&handler);
  KeywordOption *opt = 0;
  s.AddOption(SolverOptionPtr(opt = new KeywordOption()));
  s.ParseOptions(Args("kwopt=42"));
  EXPECT_EQ(1u, handler.errors.size());
  EXPECT_EQ("Option \"kwopt\" doesn't accept argument", handler.errors[0]);
  EXPECT_FALSE(opt->parsed);
}

TEST(SolverTest, ParseOptionsHandlesOptionErrorsInParse) {
  struct TestOption : SolverOption {
    TestOption() : SolverOption("testopt", "") {}
    void Format(fmt::Formatter &) {}
    void Parse(const char *&s) {
      while (*s && !std::isspace(*s))
        ++s;
      throw OptionError("test message");
    }
  };
  TestSolver s;
  TestErrorHandler handler;
  s.set_error_handler(&handler);
  s.AddOption(SolverOptionPtr(new TestOption()));
  s.ParseOptions(Args("testopt=1 testopt=2"));
  EXPECT_EQ(2u, handler.errors.size());
  EXPECT_EQ("test message", handler.errors[0]);
  EXPECT_EQ("test message", handler.errors[1]);
}

TEST(SolverTest, NoEchoOnErrors) {
  EXPECT_EXIT({
    TestSolver s;
    FILE *f = freopen("out", "w", stdout);
    s.ParseOptions(Args("badopt=1 version=2"));
    fclose(f);
    exit(0);
  }, ::testing::ExitedWithCode(0), "");
  EXPECT_EQ("", ReadFile("out"));
}

TEST(SolverTest, OptionEcho) {
  EXPECT_EXIT({
    TestSolver s;
    FILE *f = freopen("out", "w", stdout);
    s.ParseOptions(Args("wantsol=3"));
    s.ParseOptions(Args("wantsol=5"), 0);
    s.ParseOptions(Args("wantsol=9"));
    fclose(f);
    exit(0);
  }, ::testing::ExitedWithCode(0), "");
  EXPECT_EQ("wantsol=5\n", ReadFile("out"));
}

TEST(SolverTest, ExceptionInOptionHandler) {
  class TestException {};
  struct TestSolver : public Solver<TestSolver> {
    int GetIntOption(const char *) const { return 0; }
    void Throw(const char *, int) { throw TestException(); }
    TestSolver() : Solver<TestSolver>("") {
      AddIntOption("throw", "", &TestSolver::GetIntOption, &TestSolver::Throw);
    }
    void Solve(Problem &) {}
  };
  TestSolver s;
  EXPECT_THROW(s.ParseOptions(Args("throw=1")), TestException);
}

TEST(SolverTest, IntOptions) {
  TestSolverWithOptions s;
  EXPECT_TRUE(s.ParseOptions(Args("intopt1=3", "intopt2=7")));
  EXPECT_EQ(3, s.intopt1);
  EXPECT_EQ(7, s.intopt2);
}

TEST(SolverTest, GetIntOption) {
  TestSolverWithOptions test_solver;
  BasicSolver &s = test_solver;
  EXPECT_EQ(0, s.GetIntOption("intopt1"));
  test_solver.intopt1 = 42;
  EXPECT_EQ(42, s.GetIntOption("intopt1"));
  EXPECT_THROW(s.GetDblOption("intopt1"), OptionError);
  EXPECT_THROW(s.GetStrOption("intopt1"), OptionError);
  EXPECT_THROW(s.GetIntOption("badopt"), OptionError);
}

TEST(SolverTest, SetIntOption) {
  TestSolverWithOptions test_solver;
  BasicSolver &s = test_solver;
  s.SetIntOption("intopt1", 11);
  EXPECT_EQ(11, test_solver.intopt1);
  s.SetIntOption("intopt1", 42);
  EXPECT_EQ(42, test_solver.intopt1);
  EXPECT_THROW(s.SetDblOption("intopt1", 0), OptionError);
  EXPECT_THROW(s.SetStrOption("intopt1", ""), OptionError);
  EXPECT_THROW(s.SetIntOption("badopt", 0), OptionError);
}

TEST(SolverTest, DblOptions) {
  TestSolverWithOptions s;
  EXPECT_TRUE(s.ParseOptions(Args("dblopt2=1.3", "dblopt1=5.4")));
  EXPECT_EQ(5.4, s.dblopt1);
  EXPECT_EQ(1.3, s.dblopt2);
}

TEST(SolverTest, GetDblOption) {
  TestSolverWithOptions test_solver;
  BasicSolver &s = test_solver;
  EXPECT_EQ(0, s.GetDblOption("dblopt1"));
  test_solver.dblopt1 = 42;
  EXPECT_EQ(42, s.GetDblOption("dblopt1"));
  EXPECT_THROW(s.GetIntOption("dblopt1"), OptionError);
  EXPECT_THROW(s.GetStrOption("dblopt1"), OptionError);
  EXPECT_THROW(s.GetDblOption("badopt"), OptionError);
}

TEST(SolverTest, SetDblOption) {
  TestSolverWithOptions test_solver;
  BasicSolver &s = test_solver;
  s.SetDblOption("dblopt1", 1.1);
  EXPECT_EQ(1.1, test_solver.dblopt1);
  s.SetDblOption("dblopt1", 4.2);
  EXPECT_EQ(4.2, test_solver.dblopt1);
  EXPECT_THROW(s.SetIntOption("dblopt1", 0), OptionError);
  EXPECT_THROW(s.SetStrOption("dblopt1", ""), OptionError);
  EXPECT_THROW(s.SetDblOption("badopt", 0), OptionError);
}

TEST(SolverTest, StrOptions) {
  TestSolverWithOptions s;
  EXPECT_TRUE(s.ParseOptions(Args("stropt1=abc", "stropt2=def")));
  EXPECT_EQ("abc", s.stropt1);
  EXPECT_EQ("def", s.stropt2);
}

TEST(SolverTest, GetStrOption) {
  TestSolverWithOptions test_solver;
  BasicSolver &s = test_solver;
  EXPECT_EQ("", s.GetStrOption("stropt1"));
  test_solver.stropt1 = "abc";
  EXPECT_EQ("abc", s.GetStrOption("stropt1"));
  EXPECT_THROW(s.GetIntOption("stropt1"), OptionError);
  EXPECT_THROW(s.GetDblOption("stropt1"), OptionError);
  EXPECT_THROW(s.GetStrOption("badopt"), OptionError);
}

TEST(SolverTest, SetStrOption) {
  TestSolverWithOptions test_solver;
  BasicSolver &s = test_solver;
  s.SetStrOption("stropt1", "abc");
  EXPECT_EQ("abc", test_solver.stropt1);
  s.SetStrOption("stropt1", "def");
  EXPECT_EQ("def", test_solver.stropt1);
  EXPECT_THROW(s.SetIntOption("stropt1", 0), OptionError);
  EXPECT_THROW(s.SetDblOption("stropt1", 0), OptionError);
  EXPECT_THROW(s.SetStrOption("badopt", ""), OptionError);
}

TEST(SolverTest, VersionOption) {
  TestSolver s("testsolver", "Test Solver");
  EXPECT_EXIT({
    FILE *f = freopen("out", "w", stdout);
    s.ParseOptions(Args("version"));
    fclose(f);
    exit(0);
  }, ::testing::ExitedWithCode(0), "");
  fmt::Formatter format;
  format("Test Solver ({}), ASL({})\n") << sysdetails_ASL << ASLdate_ASL;
  EXPECT_EQ(format.str(), ReadFile("out"));
}

TEST(SolverTest, VersionOptionReset) {
  TestSolver s("testsolver", "Test Solver");
  EXPECT_EXIT({
    FILE *f = freopen("out", "w", stdout);
    s.ParseOptions(Args("version"));
    printf("end\n");
    s.ParseOptions(Args(0));
    fclose(f);
    exit(0);
  }, ::testing::ExitedWithCode(0), "");
  fmt::Formatter format;
  format("Test Solver ({}), ASL({})\nend\n") << sysdetails_ASL << ASLdate_ASL;
  EXPECT_EQ(format.str(), ReadFile("out"));
}

TEST(SolverTest, WantsolOption) {
  TestSolver s("");
  EXPECT_EQ(0, s.wantsol());
  EXPECT_TRUE(s.ParseOptions(Args("wantsol=1")));
  EXPECT_EQ(1, s.wantsol());
  EXPECT_TRUE(s.ParseOptions(Args("wantsol=5")));
  EXPECT_EQ(5, s.wantsol());
}
