/*
 A C++ interface to an AMPL solver.

 Copyright (C) 2012 AMPL Optimization Inc

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that the copyright notice and this permission notice and warranty
 disclaimer appear in supporting documentation.

 The author and AMPL Optimization Inc disclaim all warranties with
 regard to this software, including all implied warranties of
 merchantability and fitness.  In no event shall the author be liable
 for any special, indirect or consequential damages or any damages
 whatsoever resulting from loss of use, data or profits, whether in an
 action of contract, negligence or other tortious action, arising out
 of or in connection with the use or performance of this software.

 Author: Victor Zverovich
 */

#ifndef SOLVERS_UTIL_SOLVER_H_
#define SOLVERS_UTIL_SOLVER_H_

#include <cctype>
#include <csignal>
#include <cstring>

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "solvers/getstub.h"
#undef Char
}

#include "solvers/util/problem.h"

namespace ampl {

// Formats a double with objective precision.
// Usage: fmt::Format("{}") << ObjPrec(42.0);
class ObjPrec {
 private:
  double value_;

 public:
  explicit ObjPrec(double value) : value_(value) {}

  friend void Format(fmt::Writer &w, const fmt::FormatSpec &spec, ObjPrec op) {
    char buffer[32];
    g_fmtop(buffer, op.value_);
    w.Write(buffer, spec);
  }
};

namespace internal {

// Indents a string and breaks it into lines of 78 characters or less
// by word wrapping.
std::string IndentAndWordWrap(fmt::StringRef s, int indent = 0);

// A helper class for implementing an option of type T.
template <typename T>
struct OptionHelper;

template <>
struct OptionHelper<int> {
  typedef int Arg;
  static const char TYPE_NAME[];
  static void Write(fmt::Writer &w, Arg value) { w << value; }
  static int Parse(const char *&s);
  static int CastArg(int value) { return value; }
};

template <>
struct OptionHelper<double> {
  typedef double Arg;
  static const char TYPE_NAME[];
  static void Write(fmt::Writer &w, Arg value);
  static double Parse(const char *&s);
  static double CastArg(double value) { return value; }
};

template <>
struct OptionHelper<std::string> {
  typedef const char *Arg;
  static const char TYPE_NAME[];
  static void Write(fmt::Writer &w, const std::string &s) { w << s; }
  static std::string Parse(const char *&s);
  static const char *CastArg(const std::string &s) { return s.c_str(); }
};
}

// An interface for receiving errors reported via Solver::ReportError.
class ErrorHandler {
 protected:
  ~ErrorHandler() {}

 public:
  virtual void HandleError(fmt::StringRef message) = 0;
};

// An interface for receiving solver output.
class OutputHandler {
 protected:
  ~OutputHandler() {}

 public:
  virtual void HandleOutput(fmt::StringRef output) = 0;
};

// An interface for receiving solutions.
class SolutionHandler {
 protected:
  ~SolutionHandler() {}

 public:
  // Receives a feasible solution.
  virtual void HandleFeasibleSolution(Problem &p, fmt::StringRef message,
      const double *values, const double *dual_values, double obj_value) = 0;

  // Receives a final solution or a notification that the problem is
  // infeasible or unbounded.
  virtual void HandleSolution(Problem &p, fmt::StringRef message,
      const double *values, const double *dual_values, double obj_value) = 0;
};

class BasicSolutionHandler : public SolutionHandler {
 protected:
  ~BasicSolutionHandler() {}

 public:
  virtual void HandleFeasibleSolution(Problem &, fmt::StringRef,
      const double *, const double *, double) {}
  virtual void HandleSolution(Problem &, fmt::StringRef,
      const double *, const double *, double) {}
};

class Interruptible {
 protected:
  ~Interruptible() {}

 public:
  virtual void Interrupt() = 0;
};

class Solver;

// A signal handler.
// When a solver is run in a terminal it should respond to SIGINT (Ctrl-C)
// by interrupting its execution and returning the best solution found.
// This can be done in two ways. The first one is to check the return value
// of SignalHandler::stop() periodically. The second is to register an object
// that implements the Interruptible interface. In both cases you should
// create a SignalHandler object before solving.
class SignalHandler {
 private:
  static std::string signal_message_;
  static const char *signal_message_ptr_;
  static unsigned signal_message_size_;
  static volatile std::sig_atomic_t stop_;
  static Interruptible *interruptible_;

  static void HandleSigInt(int sig);

 public:
  explicit SignalHandler(const Solver &s, Interruptible *i = 0);

  ~SignalHandler() {
    stop_ = 1;
    interruptible_ = 0;
  }

  // Returns true if the execution should be stopped due to SIGINT.
  static bool stop() { return stop_ != 0; }
};

// An option error.
class OptionError : public Error {
public:
  explicit OptionError(fmt::StringRef message) : Error(message) {}
};

// An exception thrown when an invalid value is provided for an option.
class InvalidOptionValue : public OptionError {
 public:
  template <typename T>
  InvalidOptionValue(fmt::StringRef name, T value)
  : OptionError(fmt::Format("Invalid value \"{}\" for option \"{}\"")
      << value << name.c_str()) {}
};

// A solver option.
class SolverOption {
 private:
  std::string name_;
  std::string description_;
  bool is_keyword_;

 public:
  SolverOption(const char *name,
      const char *description, bool is_keyword = false)
  : name_(name), description_(description), is_keyword_(is_keyword) {}
  virtual ~SolverOption() {}

  // Returns the option name.
  const char *name() const { return name_.c_str(); }

  // Returns the option description.
  const char *description() const { return description_.c_str(); }

  // Returns true if this is a keyword option, i.e. an option that
  // doesn't take a value.
  bool is_keyword() const { return is_keyword_; }

  // Formats the option value. Throws OptionError in case of error.
  virtual void Write(fmt::Writer &w) = 0;

  // Parses a string and sets the option value. Throws InvalidOptionValue
  // if the value is invalid or OptionError in case of another error.
  virtual void Parse(const char *&s) = 0;
};

template <typename T>
class TypedSolverOption : public SolverOption {
 public:
  TypedSolverOption(const char *name, const char *description)
  : SolverOption(name, description) {}

  void Write(fmt::Writer &w) {
    internal::OptionHelper<T>::Write(w, GetValue());
  }

  void Parse(const char *&s) {
    const char *start = s;
    T value = internal::OptionHelper<T>::Parse(s);
    if (*s && !std::isspace(*s)) {
      do ++s;
      while (*s && !std::isspace(*s));
      throw InvalidOptionValue(name(), std::string(start, s - start));
    }
    SetValue(internal::OptionHelper<T>::CastArg(value));
  }

  // Returns the option value.
  virtual T GetValue() const = 0;

  // Sets the option value or throws InvalidOptionValue if the value is invalid.
  virtual void SetValue(typename internal::OptionHelper<T>::Arg value) = 0;
};

template <typename T>
inline const T *ptr(const std::vector<T> &v) { return v.empty() ? 0 : &v[0]; }

// An AMPL solver.
//
// Example:
//
// class MySolver : public Solver {
//  public:
//   void GetTestOption(const char *name, int value, int info) {
//     // Returns the option value; info is an arbitrary value passed as
//     // the last argument to AddIntOption. It can be useful if the same
//     // function handles multiple options.
//     ...
//   }
//   void SetTestOption(const char *name, int value, int info) {
//     // Set the option value; info is the same as in GetTestOption.
//     ...
//   }
//
//   MySolver()  {
//     AddIntOption("test", "This is a test option",
//                  &MySolver::GetTestOption, &MySolver::SetTestOption, 42);
//   }
// };
class Solver
  : private ErrorHandler, private OutputHandler, private Option_Info {
 private:
  std::string name_;
  std::string long_name_;
  std::string options_var_name_;
  std::string version_;

  bool has_errors_;
  OutputHandler *output_handler_;
  ErrorHandler *error_handler_;
  SolutionHandler *sol_handler_;

  // The filename stub for returning multiple solutions.
  std::string solution_stub_;

  // The number of feasible solutions found so far.
  int num_solutions_;

  // Specifies whether to return the number of solutions in the .nsol suffix.
  bool count_solutions_;

  class SolutionWriter : public SolutionHandler {
   private:
    Solver *solver_;

   public:
    SolutionWriter() : solver_() {}
    void set_solver(Solver *s) { solver_ = s; }

    void HandleFeasibleSolution(Problem &p, fmt::StringRef message,
          const double *values, const double *dual_values, double);
    void HandleSolution(Problem &p, fmt::StringRef message,
          const double *values, const double *dual_values, double);
  };
  SolutionWriter sol_writer_;

  unsigned read_flags_;  // flags passed to Problem::Read

  struct OptionNameLess {
    bool operator()(const char *lhs, const char *rhs) const;
  };

  typedef std::map<const char*, SolverOption*, OptionNameLess> OptionMap;
  OptionMap options_;
  keyword cl_option_;  // command-line option '='

  bool timing_;

  std::vector<SufDecl> suffixes_;

  void RegisterSuffixes(Problem &p);

  static char *PrintOptionsAndExit(Option_Info *oi, keyword *kw, char *value);

  void HandleOutput(fmt::StringRef output) {
    std::fputs(output.c_str(), stdout);
  }

  void HandleError(fmt::StringRef message) {
    std::fputs(message.c_str(), stderr);
    std::fputc('\n', stderr);
  }

  class ErrorReporter {
   private:
    ErrorHandler *handler_;

   public:
    explicit ErrorReporter(ErrorHandler *h) : handler_(h) {}

    void operator()(const fmt::Writer &w) const {
      handler_->HandleError(fmt::StringRef(w.c_str(), w.size()));
    }
  };

  // Returns the option with specified name.
  SolverOption *FindOption(const char *name) const;

  template <typename T>
  T GetOptionValue(const char *name) const {
    const TypedSolverOption<T> *opt =
        dynamic_cast<TypedSolverOption<T> *>(FindOption(name));
    if (!opt)
      throw OptionTypeError(name, internal::OptionHelper<T>::TYPE_NAME);
    return opt->GetValue();
  }

  template <typename T>
  void SetOptionValue(const char *name,
      typename internal::OptionHelper<T>::Arg value) {
    TypedSolverOption<T> *opt =
        dynamic_cast<TypedSolverOption<T> *>(FindOption(name));
    if (!opt)
      throw OptionTypeError(name, internal::OptionHelper<T>::TYPE_NAME);
    opt->SetValue(value);
  }

  static OptionError OptionTypeError(fmt::StringRef name, fmt::StringRef type) {
    return OptionError(fmt::Format("Option \"{}\" is not of type \"{}\"")
            << name.c_str() << type.c_str());
  }

  // Parses an option string.
  void ParseOptionString(const char *s, unsigned flags);

  // Handler should be a class derived from Solver that will receive
  // notifications about parsed options.
  template <typename Handler, typename T>
  class ConcreteOption : public TypedSolverOption<T> {
   private:
    typedef typename internal::OptionHelper<T>::Arg Arg;
    typedef T (Handler::*Get)(const char *) const;
    typedef void (Handler::*Set)(const char *, Arg);

    Handler &handler_;
    Get get_;
    Set set_;

   public:
    ConcreteOption(const char *name,
        const char *description, Solver *s, Get get, Set set)
    : TypedSolverOption<T>(name, description),
      handler_(static_cast<Handler&>(*s)), get_(get), set_(set) {}

    T GetValue() const { return (handler_.*get_)(this->name()); }
    void SetValue(Arg value) { (handler_.*set_)(this->name(), value); }
  };

  template <typename Handler, typename T,
            typename Info, typename InfoArg = Info>
  class ConcreteOptionWithInfo : public TypedSolverOption<T> {
   private:
    typedef typename internal::OptionHelper<T>::Arg Arg;
    typedef T (Handler::*Get)(const char *, InfoArg) const;
    typedef void (Handler::*Set)(const char *, Arg, InfoArg);

    Handler &handler_;
    Get get_;
    Set set_;
    Info info_;

   public:
    ConcreteOptionWithInfo(const char *name, const char *description,
        Solver *s, Get get, Set set, InfoArg info)
    : TypedSolverOption<T>(name, description),
      handler_(static_cast<Handler&>(*s)), get_(get), set_(set), info_(info) {}

    T GetValue() const { return (handler_.*get_)(this->name(), info_); }
    void SetValue(Arg value) { (handler_.*set_)(this->name(), value, info_); }
  };

 public:
#ifdef HAVE_UNIQUE_PTR
  typedef std::unique_ptr<SolverOption> OptionPtr;
#else
  typedef std::auto_ptr<SolverOption> OptionPtr;
  static OptionPtr move(OptionPtr p) { return p; }
#endif

 protected:
  // Flags for the Solver constructor.
  enum {
    // Multiple solution support.
    // Makes Solver register "countsolutions" and "solutionstub" options
    // and write every solution passed to HandleFeastibleSolution to a file
    // solutionstub & i & ".sol" where i is a solution number.
    MULTIPLE_SOL = 1
  };

  // Constructs a Solver object.
  // date: The solver date in YYYYMMDD format.
  Solver(fmt::StringRef name, fmt::StringRef long_name = 0,
      long date = 0, unsigned flags = 0);

  void set_long_name(fmt::StringRef name) {
    long_name_ = name;
    bsname = const_cast<char*>(long_name_.c_str());
  }

  void set_version(fmt::StringRef version) {
    version_ = version;
    Option_Info::version = const_cast<char*>(version_.c_str());
  }

  // Sets the flags for Problem::Read.
  void set_read_flags(unsigned flags) { read_flags_ = flags; }

  virtual std::string GetOptionHeader() { return std::string(); }

  virtual void DoSolve(Problem &p) = 0;

  bool need_multiple_solutions() const {
    return count_solutions_ || !solution_stub_.empty();
  }

  // Passes a solution to the solution handler.
  void HandleSolution(Problem &p, fmt::StringRef message,
      const double *values, const double *dual_values,
      double obj_value = std::numeric_limits<double>::quiet_NaN()) {
    sol_handler_->HandleSolution(p, message, values, dual_values, obj_value);
  }

  // Passes a feasible solution to the solution handler.
  void HandleFeasibleSolution(Problem &p, fmt::StringRef message,
      const double *values, const double *dual_values,
      double obj_value = std::numeric_limits<double>::quiet_NaN()) {
    sol_handler_->HandleFeasibleSolution(
        p, message, values, dual_values, obj_value);
  }

  void AddOption(OptionPtr opt) {
    // First insert the option, then release a pointer to it. Doing the other
    // way around may lead to a memory leak if insertion throws an exception.
    options_[opt->name()] = opt.get();
    opt.release();
  }

  // Adds an integer option. The arguments get and set should be
  // pointers to member functions in the solver class. They are used to
  // get and set an option value respectively.
  template <typename Handler>
  void AddIntOption(const char *name, const char *description,
      int (Handler::*get)(const char *) const,
      void (Handler::*set)(const char *, int)) {
    AddOption(OptionPtr(new ConcreteOption<Handler, int>(
        name, description, this, get, set)));
  }

  // Adds an integer option with additional information. The arguments get
  // and set should be pointers to member functions in the solver class.
  // They are used to get and set an option value respectively.
  template <typename Handler, typename Info>
  void AddIntOption(const char *name, const char *description,
      int (Handler::*get)(const char *, const Info &) const,
      void (Handler::*set)(const char *, int, const Info &),
      const Info &info) {
    AddOption(OptionPtr(
        new ConcreteOptionWithInfo<Handler, int, Info, const Info &>(
            name, description, this, get, set, info)));
  }

  // The same as above but with Info argument passed by value.
  template <typename Handler, typename Info>
  void AddIntOption(const char *name, const char *description,
      int (Handler::*get)(const char *, Info) const,
      void (Handler::*set)(const char *, int, Info), Info info) {
    AddOption(OptionPtr(new ConcreteOptionWithInfo<Handler, int, Info>(
            name, description, this, get, set, info)));
  }

  // Adds a double option. The arguments get and set should be
  // pointers to member functions in the solver class. They are used to
  // get and set an option value respectively.
  template <typename Handler>
  void AddDblOption(const char *name, const char *description,
      double (Handler::*get)(const char *) const,
      void (Handler::*set)(const char *, double)) {
    AddOption(OptionPtr(new ConcreteOption<Handler, double>(
        name, description, this, get, set)));
  }

  // Adds a double option with additional information. The arguments get
  // and set should be pointers to member functions in the solver class.
  // They are used to get and set an option value respectively.
  template <typename Handler, typename Info>
  void AddDblOption(const char *name, const char *description,
      double (Handler::*get)(const char *, const Info &) const,
      void (Handler::*set)(const char *, double, const Info &),
      const Info &info) {
    AddOption(OptionPtr(
        new ConcreteOptionWithInfo<Handler, double, Info, const Info &>(
            name, description, this, get, set, info)));
  }

  // The same as above but with Info argument passed by value.
  template <typename Handler, typename Info>
  void AddDblOption(const char *name, const char *description,
      double (Handler::*get)(const char *, Info) const,
      void (Handler::*set)(const char *, double, Info), Info info) {
    AddOption(OptionPtr(new ConcreteOptionWithInfo<Handler, double, Info>(
            name, description, this, get, set, info)));
  }

  // Adds a string option. The arguments get and set should be
  // pointers to member functions in the solver class. They are used to
  // get and set an option value respectively.
  template <typename Handler>
  void AddStrOption(const char *name, const char *description,
      std::string (Handler::*get)(const char *) const,
      void (Handler::*set)(const char *, const char *)) {
    AddOption(OptionPtr(new ConcreteOption<Handler, std::string>(
        name, description, this, get, set)));
  }

  // Adds a string option with additional information. The arguments get
  // and set should be pointers to member functions in the solver class.
  // They are used to get and set an option value respectively.
  template <typename Handler, typename Info>
  void AddStrOption(const char *name, const char *description,
      std::string (Handler::*get)(const char *, const Info &) const,
      void (Handler::*set)(const char *, const char *, const Info &),
      const Info &info) {
    AddOption(OptionPtr(
        new ConcreteOptionWithInfo<Handler, std::string, Info, const Info &>(
            name, description, this, get, set, info)));
  }

  // The same as above but with Info argument passed by value.
  template <typename Handler, typename Info>
  void AddStrOption(const char *name, const char *description,
      std::string (Handler::*get)(const char *, Info) const,
      void (Handler::*set)(const char *, const char *, Info), Info info) {
    typedef void (Handler::*Func)(const char *, const char *, Info);
    AddOption(OptionPtr(new ConcreteOptionWithInfo<Handler, std::string, Info>(
            name, description, this, get, set, info)));
  }

  virtual void HandleUnknownOption(const char *name) {
    ReportError("Unknown option \"{}\"") << name;
  }

  // Adds a suffix.
  void AddSuffix(const char *name, const char *table, int kind, int nextra = 0);

  class Printer {
   private:
    OutputHandler *handler_;

   public:
    explicit Printer(OutputHandler *h) : handler_(h) {}

    void operator()(const fmt::Writer &w) const {
      handler_->HandleOutput(fmt::StringRef(w.c_str(), w.size()));
    }
  };

  Printer MakePrinter() { return Printer(output_handler_); }

 public:
  virtual ~Solver();

  // Returns the solver name.
  const char *name() const { return sname; }

  // Returns the long solver name.
  // This name is used in startup "banner".
  const char *long_name() const { return bsname; }

  // Returns the name of <solver>_options environment variable.
  const char *options_var_name() const { return opname; }

  // Returns the solver version.
  const char *version() const { return Option_Info::version; }

  // Returns the solver date in YYYYMMDD format.
  long date() const { return driver_date; }

  // Returns the solver flags.
  // Possible values that can be combined with bitwise OR:
  //   ASL_OI_want_funcadd
  //   ASL_OI_keep_underscores
  //   ASL_OI_show_version
  int flags() const { return Option_Info::flags; }

  // Returns the value of the wantsol option which specifies what solution
  // information to write in a stand-alone invocation (no -AMPL on the
  // command line).  Possible values that can be combined with bitwise OR:
  //   1 = write .sol file
  //   2 = primal variables to stdout
  //   4 = dual variables to stdout
  //   8 = suppress solution message
  int wantsol() const { return Option_Info::wantsol; }

  // Returns true if the timing is enabled.
  bool timing() const { return timing_; }

  // Returns the error handler.
  ErrorHandler *error_handler() { return error_handler_; }

  // Sets the error handler.
  void set_error_handler(ErrorHandler *eh) { error_handler_ = eh; }

  // Returns the output handler.
  OutputHandler *output_handler() { return output_handler_; }

  // Sets the output handler.
  void set_output_handler(OutputHandler *oh) { output_handler_ = oh; }

  // Returns the solution handler.
  SolutionHandler *solution_handler() { return sol_handler_; }

  // Sets the solution handler.
  void set_solution_handler(SolutionHandler *sh) { sol_handler_ = sh; }

  // Processes command-line arguments, reads a problem from an .nl file
  // if the file name (stub) is specified and parses solver options.
  // Returns true if the arguments contain the file name and options were
  // parsed successfully; false otherwise.
  // If there was an  error parsing arguments or reading the problem
  // ProcessArgs will print an error message and call std::exit (this is
  // likely to change in the future version).
  bool ProcessArgs(char **&argv, Problem &p, unsigned flags = 0);

  // Flags for ParseOptions.
  enum {
    // Don't echo options during parsing.
    NO_OPTION_ECHO = 1
  };

  // Parses solver options and returns true if there were no errors and
  // false otherwise. It accepts a pointer to the problem because some
  // options may depend on problem features.
  virtual bool ParseOptions(
      char **argv, unsigned flags = 0, const Problem *p = 0);

  // Returns the value of an integer option.
  // Throws OptionError if there is no such option or it has a different type.
  int GetIntOption(const char *name) const {
    return GetOptionValue<int>(name);
  }

  // Sets the value of an integer option.
  // Throws OptionError if there is no such option or it has a different type.
  void SetIntOption(const char *name, int value) {
    SetOptionValue<int>(name, value);
  }

  // Returns the value of a double option.
  // Throws OptionError if there is no such option or it has a different type.
  double GetDblOption(const char *name) const {
    return GetOptionValue<double>(name);
  }

  // Sets the value of a double option.
  // Throws OptionError if there is no such option or it has a different type.
  void SetDblOption(const char *name, double value) {
    SetOptionValue<double>(name, value);
  }

  // Returns the value of a string option.
  // Throws OptionError if there is no such option or it has a different type.
  std::string GetStrOption(const char *name) const {
    return GetOptionValue<std::string>(name);
  }

  // Sets the value of a string option.
  // Throws OptionError if there is no such option or it has a different type.
  void SetStrOption(const char *name, const char *value) {
    SetOptionValue<std::string>(name, value);
  }

  // Reports an error printing the formatted error message to stderr.
  // Usage: ReportError("File not found: {}") << filename;
  fmt::Formatter<ErrorReporter> ReportError(fmt::StringRef format) {
    has_errors_ = true;
    return fmt::Formatter<ErrorReporter>(format, ErrorReporter(error_handler_));
  }

  // Formats a string and prints it to stdout or, if an output handler
  // is registered, sends it to the output handler.
  fmt::Formatter<Printer> Print(fmt::StringRef format) {
    return fmt::Formatter<Printer>(format, Printer(output_handler_));
  }

  // Solves a problem.
  // The solutions are reported via the registered solution handler.
  void Solve(Problem &p);

  // Runs the solver.
  int Run(char **argv);
};
}

#endif  // SOLVERS_UTIL_SOLVER_H_
