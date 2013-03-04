/*
 Utilities for writing AMPL solvers.

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

#include "solvers/util/solver.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
# include <unistd.h>
# define AMPL_WRITE write
#else
# include <io.h>
# define AMPL_WRITE _write
#endif

#include "solvers/util/format.h"
#include "solvers/getstub.h"

namespace {

struct KeywordNameLess {
  bool operator()(const keyword &lhs, const keyword &rhs) const {
    return std::strcmp(lhs.name, rhs.name) < 0;
  }
};

// Writes a linear term.
void WriteTerm(fmt::Writer &w, double coef, unsigned var) {
  if (coef != 1)
    w << coef << " * ";
  w << "x" << (var + 1);
}

template <typename LinearExpr>
void WriteExpr(fmt::Writer &w, LinearExpr expr) {
  bool has_terms = false;
  typedef typename LinearExpr::iterator Iterator;
  for (Iterator i = expr.begin(), e = expr.end(); i != e; ++i) {
    double coef = i->coef();
    if (coef != 0) {
      if (has_terms)
        w << " + ";
      else
        has_terms = true;
      WriteTerm(w, coef, i->var_index());
    }
  }
  if (!has_terms)
    w << "0";
}
}

namespace ampl {

namespace internal {
int OptionParser<int>::operator()(Option_Info *oi, keyword *kw, char *&s) {
  keyword thiskw(*kw);
  int value = 0;
  thiskw.info = &value;
  s = I_val(oi, &thiskw, s);
  return value;
}

double OptionParser<double>::operator()(
    Option_Info *oi, keyword *kw, char *&s) {
  keyword thiskw(*kw);
  double value = 0;
  thiskw.info = &value;
  s = D_val(oi, &thiskw, s);
  return value;
}

const char* OptionParser<const char*>::operator()(
    Option_Info *, keyword *, char *&s) {
  char *end = s;
  while (*end && !isspace(*end))
    ++end;
  value_.assign(s, end - s);
  s = end;
  return value_.c_str();
}
}

void Solution::Read(const char *stub, int num_vars, int num_cons) {
  // Allocate filename large enough to hold stub, ".nl" and terminating zero.
  std::size_t stub_len = std::strlen(stub);
  std::vector<char> filename(stub_len + 4);
  std::strcpy(&filename[0], stub);
  ASL asl = {};
  asl.i.n_var_ = num_vars;
  asl.i.n_con_ = num_cons;
  asl.i.ASLtype = 1;
  asl.i.filename_ = &filename[0];
  asl.i.stub_end_ = asl.i.filename_ + stub_len;
  Solution sol;
  char *message = read_sol_ASL(&asl, &sol.values_, &sol.dual_values_);
  if (!message)
    throw Error("Error reading solution file");
  free(message);
  Swap(sol);
  solve_code_ = asl.p.solve_code_;
}

Problem::Problem() : asl_(reinterpret_cast<ASL_fg*>(ASL_alloc(ASL_read_fg))) {}

Problem::~Problem() {
  ASL_free(reinterpret_cast<ASL**>(&asl_));
}

fmt::Writer &operator<<(fmt::Writer &w, const Problem &p) {
  // Write variables.
  int num_vars = p.num_vars();
  for (int i = 0; i < num_vars; ++i) {
    w << "var x" << (i + 1);
    double lb = p.var_lb(i), ub = p.var_ub(i);
    if (lb != -Infinity)
      w << " >= " << lb;
    if (ub != Infinity)
      w << " <= " << ub;
    w << ";\n";
  }

  // Write objectives.
  for (int i = 0, n = p.num_objs(); i < n; ++i) {
    w << (p.obj_type(i) == MIN ? "minimize" : "maximize") << " o: ";
    WriteExpr(w, p.linear_obj_expr(i));
    w << ";\n";
  }

  // Write constraints.
  for (int i = 0, n = p.num_cons(); i < n; ++i) {
    w << "s.t. c" << (i + 1) << ": ";
    double lb = p.con_lb(i), ub = p.con_ub(i);
    if (lb != ub && lb != -Infinity && ub != Infinity)
      w << lb << " <= ";
    WriteExpr(w, p.linear_con_expr(i));
    if (lb == ub)
      w << " = " << lb;
    else if (ub != Infinity)
      w << " <= " << ub;
    else if (lb != -Infinity)
      w << " >= " << lb;
    w << ";\n";
  }
  return w;
}

// Temporary file manager.
class TempFiles : Noncopyable {
 private:
  char *name_;

 public:
  explicit TempFiles(const AmplExports *ae) : name_(ae->Tempnam(0, 0)) {}
  ~TempFiles() {
    std::remove(c_str(fmt::Format("{}.nl") << name_));
    std::remove(c_str(fmt::Format("{}.sol") << name_));
    free(name_);
  }

  const char *stub() const { return name_; }
};

void Problem::Solve(Solution &sol, ProblemChanges *pc, unsigned flags) {
  TempFiles temp(asl_->i.ae);

  // Write an .nl file.
  int nfunc = asl_->i.nfunc_;
  if ((flags & IGNORE_FUNCTIONS) != 0)
    asl_->i.nfunc_ = 0;
  int result = fg_write_ASL(reinterpret_cast<ASL*>(asl_),
      temp.stub(), pc ? pc->vco() : 0, ASL_write_ASCII);
  asl_->i.nfunc_ = nfunc;
  if (result)
    throw Error("Error writing .nl file");

  // Run the solver and read the solution file.
  std::system(c_str(fmt::Format("cplex {} -AMPL") << temp.stub()));
  sol.Read(temp.stub(), num_vars() + (pc ? pc->num_vars() : 0),
      num_cons() + (pc ? pc->num_cons() : 0));
}

NewVCO *ProblemChanges::vco() {
  vco_.nnv = static_cast<int>(var_lb_.size());
  vco_.nnc = static_cast<int>(cons_.size());
  vco_.nno = static_cast<int>(objs_.size());
  vco_.LUnv = &var_lb_[0];
  vco_.Unv = &var_ub_[0];
  vco_.LUnc = &con_lb_[0];
  vco_.Unc = &con_ub_[0];
  vco_.newc = &cons_[0];
  vco_.newo = &objs_[0];
  vco_.ot = &obj_types_[0];
  return &vco_;
}

void ProblemChanges::AddObj(
    ObjType type, unsigned size, const double *coefs, const int *vars) {
  std::size_t start = obj_terms_.size();
  obj_terms_.resize(start + size);
  ograd dummy;
  ograd *prev = &dummy;
  for (unsigned i = 0; i < size; ++i) {
    ograd &term = obj_terms_[start + i];
    term.coef = coefs[i];
    term.varno = vars[i];
    prev->next = &term;
    prev = &term;
  }
  objs_.push_back(&obj_terms_[start]);
  obj_types_.push_back(type);
}

void ProblemChanges::AddCon(const double *coefs, double lb, double ub) {
  con_lb_.push_back(lb);
  con_ub_.push_back(ub);
  std::size_t start = con_terms_.size();
  std::size_t num_vars = problem_->num_vars() + var_lb_.size();
  con_terms_.resize(start + num_vars);
  ograd dummy;
  ograd *prev = &dummy;
  for (std::size_t i = 0; i < num_vars; ++i) {
    ograd &term = con_terms_[start + i];
    term.coef = coefs[i];
    term.varno = i;
    prev->next = &term;
    prev = &term;
  }
  cons_.push_back(&con_terms_[start]);
}

std::string SignalHandler::signal_message_;
const char *SignalHandler::signal_message_ptr_;
unsigned SignalHandler::signal_message_size_;
Interruptible *SignalHandler::interruptible_;

// Set stop_ to 1 initially to avoid accessing handler_ which may not be atomic.
volatile std::sig_atomic_t SignalHandler::stop_ = 1;

SignalHandler::SignalHandler(const BasicSolver &s, Interruptible *i) {
  signal_message_ = str(fmt::Format("\n<BREAK> ({})\n") << s.name());
  signal_message_ptr_ = signal_message_.c_str();
  signal_message_size_ = static_cast<unsigned>(signal_message_.size());
  interruptible_ = i;
  stop_ = 0;
  std::signal(SIGINT, HandleSigInt);
}

void SignalHandler::HandleSigInt(int sig) {
  unsigned count = 0;
  do {
    // Use asynchronous-safe function write instead of printf!
    int result = AMPL_WRITE(1, signal_message_ptr_ + count,
        signal_message_size_ - count);
    if (result < 0) break;
    count += result;
  } while (count < signal_message_size_);
  if (stop_) {
    // Use asynchronous-safe function _exit instead of exit!
    _exit(1);
  }
  stop_ = 1;
  if (interruptible_)
    interruptible_->Interrupt();
  // Restore the handler since it might have been reset before the handler
  // is called (this is implementation defined).
  std::signal(sig, HandleSigInt);
}

void BasicSolver::SortOptions() {
  if (options_sorted_) return;
  std::sort(keywords_.begin(), keywords_.end(), KeywordNameLess());
  keywds = &keywords_[0];
  n_keywds = static_cast<int>(keywords_.size());
  options_sorted_ = true;
}

BasicSolver::BasicSolver(
    fmt::StringRef name, fmt::StringRef long_name, long date)
: name_(name), options_sorted_(false), has_errors_(false) {
  error_handler_ = this;
  sol_handler_ = this;

  // Workaround for GCC bug 30111 that prevents value-initialization of
  // the base POD class.
  Option_Info init = {};
  Option_Info &self = *this;
  self = init;

  sname = const_cast<char*>(name_.c_str());
  if (long_name.c_str()) {
    long_name_ = long_name;
    bsname = const_cast<char*>(long_name_.c_str());
  } else {
    bsname = sname;
  }
  options_var_name_ = name_;
  options_var_name_ += "_options";
  opname = const_cast<char*>(options_var_name_.c_str());
  Option_Info::version = bsname;
  driver_date = date;

  version_desc_ = FormatDescription(
      "Single-word phrase:  report version details "
      "before solving the problem.");
  AddKeyword("version", version_desc_.c_str(), Ver_val, 0);
  wantsol_desc_ = FormatDescription(
      "In a stand-alone invocation (no -AMPL on the command line), "
      "what solution information to write.  Sum of\n"
      "      1 = write .sol file\n"
      "      2 = primal variables to stdout\n"
      "      4 = dual variables to stdout\n"
      "      8 = suppress solution message\n");
  AddKeyword("wantsol", wantsol_desc_.c_str(), WS_val, 0);
}

BasicSolver::~BasicSolver() {}

void BasicSolver::AddKeyword(const char *name,
    const char *description, Kwfunc func, const void *info) {
  keywords_.push_back(keyword());
  keyword &kw = keywords_.back();
  kw.name = const_cast<char*>(name);
  kw.desc = const_cast<char*>(description);
  kw.kf = func;
  kw.info = const_cast<void*>(info);
}

std::string BasicSolver::FormatDescription(const char *description) {
  std::ostringstream os;
  os << '\n';
  bool new_line = true;
  int line_offset = 0;
  int indent = 0;
  const char *s = description;
  const int MAX_LINE_LENGTH = 78;
  for (;;) {
    const char *start = s;
    while (*s == ' ')
      ++s;
    const char *word_start = s;
    while (*s != ' ' && *s != '\n' && *s)
      ++s;
    const char *word_end = s;
    if (new_line) {
      indent = 6 + static_cast<int>(word_start - start);
      new_line = false;
    }
    if (line_offset + (word_end - start) > MAX_LINE_LENGTH) {
      // The word doesn't fit, start a new line.
      os << '\n';
      line_offset = 0;
    }
    if (line_offset == 0) {
      // Indent the line.
      for (; line_offset < indent; ++line_offset)
        os << ' ';
      start = word_start;
    }
    os.write(start, word_end - start);
    line_offset += static_cast<int>(word_end - start);
    if (*s == '\n') {
      os << '\n';
      line_offset = 0;
      new_line = true;
      ++s;
    }
    if (!*s) break;
  }
  if (!new_line)
    os << '\n';
  return os.str();
}

bool BasicSolver::ReadProblem(char **&argv) {
  SortOptions();
  ASL_fg *aslfg = problem_.asl_;
  ASL *asl = reinterpret_cast<ASL*>(aslfg);
  char *stub = getstub_ASL(asl, &argv, this);
  if (!stub) {
    usage_noexit_ASL(this, 1);
    return false;
  }
  FILE *nl = jac0dim_ASL(asl, stub, static_cast<ftnlen>(std::strlen(stub)));
  efunc *r_ops_int[N_OPS];
  for (int i = 0; i < N_OPS; ++i)
    r_ops_int[i] = reinterpret_cast<efunc*>(i);
  aslfg->I.r_ops_ = r_ops_int;
  aslfg->p.want_derivs_ = 0;
  fg_read_ASL(asl, nl, ASL_allow_CLP | ASL_sep_U_arrays);
  aslfg->I.r_ops_ = 0;
  return true;
}
}
