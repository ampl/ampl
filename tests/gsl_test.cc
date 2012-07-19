// GSL wrapper test.

#include <functional>
#include <stdexcept>
#include <vector>

#include <gsl/gsl_math.h>
#include <gsl/gsl_sf.h>

#include "gtest/gtest.h"
#include "solvers/asl.h"
#include "tests/config.h"

using std::string;
using std::vector;

namespace {

// An immutable list of arguments for an AMPL function.
class ArgList {
 private:
  vector<real> ra;

  ArgList &operator<<(real arg) {
    ra.push_back(arg);
    return *this;
  }

 public:
  ArgList(real a0) { *this << a0; }
  ArgList(real a0, real a1) { *this << a0 << a1; }
  ArgList(real a0, real a1, real a2) { *this << a0 << a1 << a2; }
  ArgList(real a0, real a1, real a2, real a3) {
    *this << a0 << a1 << a2 << a3;
  }
  ArgList(real a0, real a1, real a2, real a3, real a4) {
    *this << a0 << a1 << a2 << a3 << a4;
  }
  ArgList(real a0, real a1, real a2, real a3, real a4, real a5) {
    *this << a0 << a1 << a2 << a3 << a4 << a5;
  }
  ArgList(real a0, real a1, real a2, real a3,
      real a4, real a5, real a6, real a7, real a8) {
    *this << a0 << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8;
  }

  const vector<real> &get() const { return ra; }

  friend std::ostream &operator<<(std::ostream &os, const ArgList &args);
};

std::ostream &operator<<(std::ostream &os, const ArgList &args) {
  os << "(";
  if (!args.ra.empty()) {
    os << args.ra.front();
    for (size_t i = 1, n = args.ra.size(); i < n; ++i)
      os << ", " << args.ra[i];
  }
  os << ")";
  return os;
}

// An immutable result of an AMPL function call.
class Result {
 private:
  real value_;
  vector<real> derivs_;
  vector<real> hes_;
  const char *error_;

 public:
  Result(real value, const vector<real> &derivs,
      const vector<real> &hes, const char *error) :
    value_(value), derivs_(derivs), hes_(hes), error_(error) {}
  operator real() const { return value_; }

  real deriv(size_t index = 0) const { return derivs_.at(index); }
  real hes(size_t index = 0) const { return hes_.at(index); }

  const char *error() const { return error_; }
};

// Function information that can't be obtained automatically, in particular
// due to limitations of numerical differentiation.
class FunctionInfo {
 public:
  virtual ~FunctionInfo() {}

  virtual double GetDerivative(const vector<real>&) const {
    return GSL_NAN;
  }

  virtual bool HasDerivative(unsigned, const vector<real>&) const {
    return true;
  }

  virtual bool HasDerivative2(const vector<real>&) const {
    return true;
  }
};

// Flags for an AMPL function call.
enum {
  ERROR      = 1, // Function call is expected to produce an error.
  DERIVS     = 2, // Get first partial derivatives.
  HES        = 6, // Get both first and second partial derivatives.
  PASS_ERROR = 8  // Pass error to the caller.
};

// An AMPL function.
class Function {
 private:
  ASL *asl_;
  func_info *fi_;
  const FunctionInfo *info_;

 public:
  Function(ASL *asl, func_info *fi, const FunctionInfo *info) :
    asl_(asl), fi_(fi), info_(info) {}

  const char *name() const { return fi_->name; }

  const FunctionInfo *info() const { return info_; }

  // Calls a function.
  // Argument vector is passed by value intentionally to avoid
  // rogue functions accidentally overwriting arguments.
  Result operator()(vector<real> args,
      int flags = 0, char *dig = 0, void *info = 0) const;

  Result operator()(const ArgList &args,
      int flags = 0, char *dig = 0, void *info = 0) const {
    return (*this)(args.get(), flags, dig, info);
  }
};

Result Function::operator()(
    vector<real> args, int flags, char *dig, void *info) const {
  // Initialize the argument list.
  arglist al = {};
  TMInfo tmi = {};
  al.ra = &args[0];
  al.nr = al.n = args.size();
  al.TMI = &tmi;
  al.AE = asl_->i.ae;
  al.dig = dig;
  al.funcinfo = info;

  // Allocate storage for the derivatives if needed.
  vector<real> derivs, hes;
  if ((flags & DERIVS) != 0) {
    derivs.resize(al.n);
    al.derivs = &derivs[0];
  }
  if ((flags & HES) == HES) {
    hes.resize(al.n * (al.n + 1) / 2);
    al.hes = &hes[0];
  }

  // Call the function.
  real value = fi_->funcp(&al);

  // Check the error message.
  if (al.Errmsg) {
    if ((flags & ERROR) == 0 && (flags & PASS_ERROR) == 0)
      ADD_FAILURE() << al.Errmsg;
  } else if ((flags & ERROR) != 0)
    ADD_FAILURE() << "Expected error in " << fi_->name;

  return Result(value, derivs, hes, al.Errmsg);
}

// A utility class for computing the derivative by Ridders' method
// of polynomial extrapolation. The implementation is taken from
// "Numerical Recipes in C", Chapter 5.7.
class Differentiator {
 private:
  enum {NTAB = 200};

  // Successive columns in the Neville tableau will go to smaller
  // step sizes and higher orders of extrapolation.
  vector<double> table_;

  // Returns the element at row i and column j of the Neville tableau.
  double& at(unsigned i, unsigned j) {
    return table_[i * NTAB + j];
  }

  // Statistics.
  static unsigned num_calls_;
  static unsigned num_infs_;
  static double min_error_;
  static double max_error_;

  static void AddStats(double deriv, double error) {
    if (gsl_isnan(deriv)) return;
    min_error_ = std::min(min_error_, error);
    max_error_ = std::max(max_error_, error);
  }

  struct StatsPrinter {
    ~StatsPrinter() {
      std::cout << "Called numerical differentiation "
          << num_calls_ << " times with " << num_infs_ << " "
          << (num_infs_ == 1 ? "infinity" : "infinities")
          << " detected" << std::endl;
      std::cout << "Error min=" << min_error_;
      std::cout << "  max=" << max_error_ << std::endl;
    }
  };

  static StatsPrinter stats_printer_;

  // Do not implement.
  Differentiator() : table_(NTAB * NTAB) {}
  ~Differentiator() {}

  template <typename F>
  static double SymmetricDifference(F f, double x, double h) {
    return (f(x + h) - f(x - h)) / (2 * h);
  }

  template <typename F>
  static double LeftDifference(F f, double x, double h) {
    return (f(x) - f(x - h)) / h;
  }

  template <typename F>
  static double RightDifference(F f, double x, double h) {
    return (f(x + h) - f(x)) / h;
  }

  // Returns the derivative of a function f at a point x by Ridders'
  // method of polynomial extrapolation.
  template <typename F, typename D>
  double operator()(F f, double x, D d, double *error = 0, double *h = 0);

 public:
  // Returns the derivative of a function f at a point x by Ridders'
  // method of polynomial extrapolation handling special cases such as
  // indeterminate and infinity.
  template <typename F>
  static double Diff(F f, double x, double *err);
};

unsigned Differentiator::num_calls_;
unsigned Differentiator::num_infs_;
double Differentiator::min_error_ = std::numeric_limits<double>::max();
double Differentiator::max_error_ = std::numeric_limits<double>::min();
Differentiator::StatsPrinter Differentiator::stats_printer_;

template <typename F, typename D>
double Differentiator::operator()(
    F f, double x, D d, double *error, double *h) {
  const double CON = 1.4, CON2 = CON * CON;
  const double SAFE = 2;
  double hh = 0.125, ans = GSL_NAN;
  at(0, 0) = d(f, x, hh);
  double err = std::numeric_limits<double>::max();
  for (unsigned i = 1; i < NTAB; i++) {
    // Try new, smaller step size.
    hh /= CON;
    at(0, i) = d(f, x, hh);
    double fac = CON2;
    for (unsigned j = 1; j <= i; j++) {
      // Compute extrapolations of various orders, requiring no new function
      // evaluations.
      at(j, i) = (at(j - 1, i) * fac - at(j - 1, i - 1)) / (fac - 1);
      fac = CON2 * fac;
      double errt = std::max(
          fabs(at(j, i) - at(j - 1, i)), fabs(at(j, i) - at(j - 1, i - 1)));
      // The error strategy is to compare each new extrapolation to one order
      // lower, both at the present step size and the previous one.
      if (errt <= err) {
        // If error is decreased, save the improved answer.
        err = errt;
        ans = at(j, i);
      }
    }
    // If higher order is worse by a significant factor SAFE, then quit early.
    if (fabs(at(i, i) - at(i - 1, i - 1)) >= SAFE * err)
      break;
  }

  if (error)
    *error = err;
  if (h)
    *h = hh;
  return ans;
}

template <typename F>
double Differentiator::Diff(F f, double x, double *error) {
  ++num_calls_;
  if (gsl_isnan(f(x)))
    return GSL_NAN;
  double h = 0;
  double dummy_error = 0;
  if (!error)
    error = &dummy_error;
  Differentiator diff;
  double deriv = diff(f, x, SymmetricDifference<F>, error, &h);
  double right_error = 0;
  double right_deriv = diff(f, x, RightDifference<F>, &right_error);
  if (gsl_isnan(deriv)) {
    AddStats(right_deriv, right_error);
    *error = right_error;
    return right_deriv;
  }
  double left_deriv = diff(f, x, LeftDifference<F>);
  if (!(fabs(left_deriv - right_deriv) <= 1e-2))
    return GSL_NAN;
  if (deriv > 1) {
    // Choose h so that x + h and x differ by a number exactly representable
    // as double. See "Numerical Recipes in C", Chapter 5.7.
    double small_h = h / 100;
    volatile double temp = x + h;
    small_h = temp - x;
    // A heuristic to detect infinity.
    double check_deriv = (f(x + small_h) - f(x - small_h)) / (2 * small_h);
    if (check_deriv > deriv * 1.1) {
      ++num_infs_;
      return GSL_POSINF;
    }
  }
  AddStats(deriv, *error);
  return deriv;
}

template <typename F>
inline double Diff(F f, double x, double *err = 0) {
  return Differentiator::Diff(f, x, err);
}

// Converts error estimate returned by Diff into an absolute tolerance to
// be used in EXPECT_NEAR.
double ConvertErrorToTolerance(double error) {
  return error != 0 ? error * 1000 : 1e-10;
}

class EvalError {
 private:
  string str;

 public:
  EvalError(const Function &af, const ArgList &args, const char *suffix = "") {
    std::ostringstream os;
    os << "can't evaluate " << af.name() << suffix << args;
    str = os.str();
  }
  operator const char*() const { return str.c_str(); }
};

// Check if the value returned by af is correct.
void CheckFunction(double value, const Function &f, const ArgList &args) {
  std::ostringstream os;
  os << "Checking if " << f.name() << args << " = " << value;
  SCOPED_TRACE(os.str());
  if (gsl_isnan(value))
    EXPECT_STREQ(EvalError(f, args), f(args, PASS_ERROR).error());
  else
    EXPECT_EQ(value, f(args)) << f.name() << args;
}

// A helper class that converts a variable index into the dig array.
// See the dig member of the arglist struct for details.
class Dig {
 private:
  char *dig_;
  vector<char> store_;

 public:
  static const unsigned NO_VAR = ~0u;

  Dig(const ArgList &args, unsigned skip_var) : dig_(0) {
    if (skip_var == NO_VAR) return;
    store_.resize(args.get().size());
    store_.at(skip_var) = 1;
    dig_ = &store_[0];
  }

  operator char*() { return dig_; }
};

// Checks if the value of the derivative returned by af agrees with the
// value returned by numerical differentiation of function f.
// var_index: index of the variable with respect to which to differentiate
// args: point at which the derivative is computed
template <typename F>
bool CheckDerivative(F f, const Function &af,
    unsigned var_index, const ArgList &args, unsigned skip_var = Dig::NO_VAR) {
  std::ostringstream os;
  os << "Checking d/dx" << var_index << " " << af.name() << " at " << args;
  SCOPED_TRACE(os.str());
  Dig dig(args, skip_var);
  double error = 0;
  double x = args.get().at(var_index);
  double numerical_deriv = Diff(f, x, &error);
  double overridden_deriv = af.info()->GetDerivative(args.get());
  if (!gsl_isnan(overridden_deriv) && overridden_deriv != numerical_deriv) {
    std::cout << "Overriding d/dx" << var_index << " " << af.name()
      << " at " << args << ", computed = " << numerical_deriv
      << ", overridden = " << overridden_deriv << std::endl;
    numerical_deriv = overridden_deriv;
  }
  if (!gsl_isnan(numerical_deriv)) {
    double deriv = af(args, DERIVS, dig).deriv(var_index);
    if (numerical_deriv != deriv)
      EXPECT_NEAR(numerical_deriv, deriv, ConvertErrorToTolerance(error));
    return true;
  }
  Result r = af(args, PASS_ERROR | DERIVS, dig);
  if (!gsl_isnan(f(x)))
    EXPECT_STREQ(EvalError(af, args, "'"), r.error());
  else
    EXPECT_TRUE(r.error() != nullptr);
  return false;
}

// A helper class that wraps a Function's derivative and binds
// all arguments except one to the given values.
class Derivative {
 private:
  Function af_;
  unsigned deriv_var_;
  unsigned eval_var_;
  vector<real> args_;
  char *dig_;

 public:
  // Creates a Derivative object.
  // eval_var:  index of a variable which is not bound
  // deriv_var: index of a variable with respect to which
  //            the derivative is taken
  Derivative(Function af, unsigned deriv_var,
      unsigned eval_var, const ArgList &args, char *dig);

  double operator()(double x);
};

Derivative::Derivative(Function af, unsigned deriv_var,
    unsigned eval_var, const ArgList &args, char *dig)
: af_(af), deriv_var_(deriv_var), eval_var_(eval_var),
  args_(args.get()), dig_(dig) {
  unsigned num_vars = args_.size();
  if (deriv_var >= num_vars || eval_var >= num_vars)
    throw std::out_of_range("variable index is out of range");
}

double Derivative::operator()(double x) {
  args_.at(eval_var_) = x;
  Result r = af_(args_, DERIVS | PASS_ERROR, dig_);
  return r.error() ? GSL_NAN : r.deriv(deriv_var_);
}

// Checks if the values of the second partial derivatives returned by af
// agree with the values returned by numerical differentiation of the first
// partial derivatives.
// args: point at which the derivatives are computed
// skip_var: index of the variable with respect to which not to differentiate
void CheckSecondDerivatives(const Function &af,
    const ArgList &args, unsigned skip_var = Dig::NO_VAR) {
  const vector<real> &ra = args.get();
  unsigned num_args = ra.size();
  Dig dig(args, skip_var);
  for (unsigned i = 0; i < num_args; ++i) {
    if (i == skip_var) continue;
    for (unsigned j = 0; j < num_args; ++j) {
      if (j == skip_var) continue;
      double error = 0;
      double d = Diff(Derivative(af, j, i, args, dig), ra[i], &error);
      std::ostringstream os;
      os << "Checking if d/dx" << i << " d/dx" << j
          << " " << af.name() << " at " << args << " is " << d;
      SCOPED_TRACE(os.str());
      if (gsl_isnan(d)) {
        af(args, ERROR | HES, dig);
        continue;
      }
      unsigned ii = i, jj = j;
      if (ii > jj) std::swap(ii, jj);
      unsigned hes_index = ii * (2 * num_args - ii - 1) / 2 + jj;
      EXPECT_NEAR(d, af(args, HES, dig).hes(hes_index),
          ConvertErrorToTolerance(error));
    }
  }
}

typedef double (*FuncU)(unsigned);
typedef double (*Func3)(double, double, double);

class GSLTest : public ::testing::Test {
 protected:
  ASL *asl;
  FunctionInfo info; // Default function info.

  void SetUp() {
    asl = ASL_alloc(ASL_read_f);
    i_option_ASL = "../solvers/gsl/libamplgsl.so";
    func_add(asl);
  }

  void TearDown() {
    ASL_free(&asl);
  }

  // Get an AMPL function by name.
  Function GetFunction(const char *name,
      const FunctionInfo &info = FunctionInfo()) const {
    func_info *fi = func_lookup(asl, name, 0);
    if (!fi)
      throw std::runtime_error(string("Function not found: ") + name);
    return Function(asl, fi, &info);
  }

  // Test a function taking a single argument.
  template <typename F>
  void TestUnaryFunc(const Function &af, F f);
  void TestFunc(const Function &af, double (*f)(double)) {
    TestUnaryFunc(af, f);
  }
  void TestFunc(const Function &af, double (*f)(double, gsl_mode_t)) {
    TestUnaryFunc(af, std::bind2nd(std::ptr_fun(f), GSL_PREC_DOUBLE));
  }

  // Test a function taking a single argument of type unsigned int.
  void TestFunc(const Function &af, FuncU f);

  void TestFunc(const Function &af, double (*f)(int, double));

  template <typename F>
  void TestBinaryFunc(const Function &af, F f);

  void TestFunc(const Function &af, double (*f)(double, double)) {
    TestBinaryFunc(af, std::ptr_fun(f));
  }

  typedef double (*Func2Mode)(double, double, gsl_mode_t);

  // Binds the mode argument of Func2Mode to GSL_PREC_DOUBLE.
  class Func2DoubleMode : public std::binary_function<double, double, double> {
   private:
    Func2Mode f_;

   public:
    Func2DoubleMode(Func2Mode f) : f_(f) {}

    double operator()(double x, double y) const {
      return f_(x, y, GSL_PREC_DOUBLE);
    }
  };

  void TestFunc(const Function &af, Func2Mode f) {
    TestBinaryFunc(af, Func2DoubleMode(f));
  }

  void TestFunc(const Function &af, Func3 f);
};

const double POINTS[] = {-5, -1.23, -1, 0, 1, 1.23, 5};
const size_t NUM_POINTS = sizeof(POINTS) / sizeof(*POINTS);

const double POINTS_FOR_N[] = {
    INT_MIN, INT_MIN + 1, -2, -1, 0, 1, 2, INT_MAX - 1, INT_MAX};
const size_t NUM_POINTS_FOR_N = sizeof(POINTS_FOR_N) / sizeof(*POINTS_FOR_N);

template <typename F>
void GSLTest::TestUnaryFunc(const Function &af, F f) {
  for (size_t i = 0; i != NUM_POINTS; ++i) {
    double x = POINTS[i];
    CheckFunction(f(x), af, x);
    CheckDerivative(f, af, 0, x);
    CheckSecondDerivatives(af, x);
  }
}

void GSLTest::TestFunc(const Function &af, FuncU f) {
  for (size_t i = 0; i != NUM_POINTS; ++i) {
    double x = POINTS[i];
    if (static_cast<unsigned>(x) == x) {
      double value = f(x);
      if (gsl_isnan(value))
        EXPECT_STREQ(EvalError(af, x), af(x, PASS_ERROR).error());
      else
        EXPECT_EQ(value, af(x)) << af.name() << " at " << x;
    } else af(x, ERROR);
    af(x, DERIVS | ERROR);
    af(x, HES | ERROR);
  }
}

void GSLTest::TestFunc(const Function &af, double (*f)(int, double)) {
  for (size_t i = 0; i != NUM_POINTS_FOR_N; ++i) {
    int n = POINTS_FOR_N[i];
    if (n < -100 || n > 100) {
      // TODO: do this through FunctionInfo
      //std::cout << "Skip testing " << name << " for n=" << n << std::endl;
      continue;
    }
    for (size_t j = 0; j != NUM_POINTS; ++j) {
      double x = POINTS[j];
      ArgList args(n, x);
      CheckFunction(f(n, x), af, args);
      af(args, DERIVS | ERROR);
      CheckDerivative(std::bind1st(std::ptr_fun(f), n), af, 1, args, 0);
      CheckSecondDerivatives(af, args, 0);
    }
  }
}

template <typename F>
void GSLTest::TestBinaryFunc(const Function &af, F f) {
  for (size_t i = 0; i != NUM_POINTS; ++i) {
    for (size_t j = 0; j != NUM_POINTS; ++j) {
      double x = POINTS[i], y = POINTS[j];
      ArgList args(x, y);
      double value = f(x, y);
      if (gsl_isnan(value)) {
        af(args, ERROR);
        continue;
      }
      EXPECT_EQ(value, af(args));
      char dig[2] = {0, 0};
      bool dx_ok = af.info()->HasDerivative(0, args.get()) &&
        CheckDerivative(std::bind2nd(f, y), af, 0, args);
      if (!dx_ok) {
        af(args, ERROR | DERIVS);
        dig[0] = 1;
      }

      double dy = af.info()->HasDerivative(1, args.get()) ?
          Diff(std::bind1st(f, x), y) : GSL_NAN;
      if (gsl_isnan(dy)) {
        af(args, ERROR | DERIVS, dig);
      } else {
        EXPECT_NEAR(dy, af(args, DERIVS, dig).deriv(1), 1e-5)
          << af.name() << " at " << x << ", " << y;
      }

      if (!af.info()->HasDerivative2(args.get()) || !dx_ok) {
        af(args, ERROR | HES);
        continue;
      }
      // TODO
      CheckSecondDerivatives(af, args);
    }
  }
}

class Bind12 {
 private:
  Func3 f_;
  double arg1_;
  double arg2_;

 public:
  Bind12(Func3 f, double arg1, double arg2)
  : f_(f), arg1_(arg1), arg2_(arg2) {}

  double operator()(double arg3) const { return f_(arg1_, arg2_, arg3); }
};

class Bind13 {
 private:
  Func3 f_;
  double arg1_;
  double arg3_;

 public:
  Bind13(Func3 f, double arg1, double arg3)
  : f_(f), arg1_(arg1), arg3_(arg3) {}

  double operator()(double arg2) const { return f_(arg1_, arg2, arg3_); }
};

class Bind23 {
 private:
  Func3 f_;
  double arg2_;
  double arg3_;

 public:
  Bind23(Func3 f, double arg2, double arg3)
  : f_(f), arg2_(arg2), arg3_(arg3) {}

  double operator()(double arg1) const { return f_(arg1, arg2_, arg3_); }
};

void GSLTest::TestFunc(const Function &af, Func3 f) {
  for (size_t i = 0; i != NUM_POINTS; ++i) {
    for (size_t j = 0; j != NUM_POINTS; ++j) {
      for (size_t k = 0; k != NUM_POINTS; ++k) {
        double x = POINTS[i], y = POINTS[j], z = POINTS[k];
        ArgList args(x, y, z);
        EXPECT_EQ(f(x, y, z), af(args)) << af.name();
        CheckDerivative(Bind23(f, y, z), af, 0, args);
        CheckDerivative(Bind13(f, x, z), af, 1, args);
        CheckDerivative(Bind12(f, x, y), af, 2, args);
        CheckSecondDerivatives(af, args);
      }
    }
  }
}

#define TEST_FUNC(name) TestFunc(GetFunction("gsl_" #name, info), gsl_##name);

TEST_F(GSLTest, TestArgList) {
  static const real ARGS[] = {5, 7, 11, 13, 17, 19, 23, 29, 31};
  EXPECT_EQ(vector<real>(ARGS, ARGS + 1), ArgList(5).get());
  EXPECT_EQ(vector<real>(ARGS, ARGS + 2), ArgList(5, 7).get());
  EXPECT_EQ(vector<real>(ARGS, ARGS + 3), ArgList(5, 7, 11).get());
  EXPECT_EQ(vector<real>(ARGS, ARGS + 4), ArgList(5, 7, 11, 13).get());
  EXPECT_EQ(vector<real>(ARGS, ARGS + 5), ArgList(5, 7, 11, 13, 17).get());
  EXPECT_EQ(vector<real>(ARGS, ARGS + 6),
      ArgList(5, 7, 11, 13, 17, 19).get());
  EXPECT_EQ(vector<real>(ARGS, ARGS + 9),
      ArgList(5, 7, 11, 13, 17, 19, 23, 29, 31).get());

  std::ostringstream oss;
  oss << ArgList(3, 5, 7);
  EXPECT_EQ("(3, 5, 7)", oss.str());
}

TEST_F(GSLTest, TestResult) {
  static const real ARGS[] = {5, 7, 11, 13, 17};
  const char *error = "brain overflow";
  Result r(42, vector<real>(ARGS, ARGS + 2),
      vector<real>(ARGS + 2, ARGS + 5), error);
  EXPECT_EQ(42, r);
  EXPECT_EQ(5, r.deriv());
  EXPECT_EQ(5, r.deriv(0));
  EXPECT_EQ(7, r.deriv(1));
  EXPECT_THROW(r.deriv(2), std::out_of_range);
  EXPECT_EQ(11, r.hes());
  EXPECT_EQ(11, r.hes(0));
  EXPECT_EQ(13, r.hes(1));
  EXPECT_EQ(17, r.hes(2));
  EXPECT_THROW(r.hes(3), std::out_of_range);
  EXPECT_STREQ(error, r.error());
}

struct CallData {
  AmplExports *ae;
  int n;
  int nr;
  vector<real> ra;
  real *derivs;
  real *hes;
  char *dig;
  char *error;
};

real Test(arglist *args) {
  CallData *data = reinterpret_cast<CallData*>(args->funcinfo);
  data->ae = args->AE;
  data->n = args->n;
  data->nr = args->nr;
  data->ra = vector<real>(args->ra, args->ra + args->n);
  data->derivs = args->derivs;
  data->hes = args->hes;
  data->dig = args->dig;
  data->error = args->Errmsg;
  if (args->ra[0] < 0)
    args->Errmsg = const_cast<char*>("oops");
  if (args->derivs) {
    args->derivs[0] = 123;
    args->derivs[1] = 456;
    if (args->n > 2)
      args->derivs[2] = 789;
  }
  if (args->hes) {
    args->hes[0] = 12;
    args->hes[1] = 34;
    args->hes[2] = 56;
  }
  return 42;
}

class TestFunction {
 private:
  ASL testASL_;
  AmplExports ae_;
  func_info fi_;
  Function f_;

 public:
  TestFunction() : testASL_(), ae_(), fi_(), f_(&testASL_, &fi_, 0) {
    testASL_.i.ae = &ae_;
    fi_.funcp = Test;
  }

  const AmplExports* ae() const { return &ae_; }
  const Function& get() const { return f_; }
};

TEST_F(GSLTest, FunctionCall) {
  TestFunction f;
  CallData data = {};
  EXPECT_EQ(42, f.get()(777, 0, 0, &data));
  EXPECT_EQ(f.ae(), data.ae);
  ASSERT_EQ(1, data.n);
  EXPECT_EQ(1, data.nr);
  EXPECT_EQ(777, data.ra[0]);
  EXPECT_TRUE(data.derivs == nullptr);
  EXPECT_TRUE(data.hes == nullptr);
  EXPECT_TRUE(data.dig == nullptr);
  EXPECT_TRUE(data.error == nullptr);
}

TEST_F(GSLTest, FunctionReturnsError) {
  TestFunction f;
  CallData data = {};
  Result r = f.get()(-1, PASS_ERROR, 0, &data);
  EXPECT_STREQ("oops", r.error());
}

TEST_F(GSLTest, FunctionReturnsDerivs) {
  TestFunction f;
  CallData data = {};
  Result res = f.get()(ArgList(11, 22, 33), DERIVS, 0, &data);
  EXPECT_EQ(42, res);
  EXPECT_EQ(f.ae(), data.ae);
  ASSERT_EQ(3, data.n);
  EXPECT_EQ(3, data.nr);
  EXPECT_EQ(11, data.ra[0]);
  EXPECT_EQ(22, data.ra[1]);
  EXPECT_EQ(33, data.ra[2]);
  EXPECT_EQ(123, res.deriv(0));
  EXPECT_EQ(456, res.deriv(1));
  EXPECT_EQ(789, res.deriv(2));
  EXPECT_THROW(res.deriv(3), std::out_of_range);
  EXPECT_TRUE(data.hes == nullptr);
  EXPECT_TRUE(data.dig == nullptr);
  EXPECT_TRUE(data.error == nullptr);
}

TEST_F(GSLTest, FunctionReturnsHes) {
  TestFunction f;
  CallData data = {};
  double ARGS[] = {111, 222};
  Result res = f.get()(vector<real>(ARGS, ARGS + 2), HES, 0, &data);
  EXPECT_EQ(42, res);
  EXPECT_EQ(f.ae(), data.ae);
  ASSERT_EQ(2, data.n);
  EXPECT_EQ(2, data.nr);
  EXPECT_EQ(111, data.ra[0]);
  EXPECT_EQ(222, data.ra[1]);
  EXPECT_EQ(123, res.deriv(0));
  EXPECT_EQ(456, res.deriv(1));
  EXPECT_THROW(res.deriv(2), std::out_of_range);
  EXPECT_EQ(12, res.hes(0));
  EXPECT_EQ(34, res.hes(1));
  EXPECT_EQ(56, res.hes(2));
  EXPECT_THROW(res.hes(3), std::out_of_range);
  EXPECT_TRUE(data.dig == nullptr);
  EXPECT_TRUE(data.error == nullptr);
}

TEST_F(GSLTest, Diff) {
  double error = GSL_NAN;
  EXPECT_NEAR(1, Diff(sin, 0, &error), 1e-7);
  EXPECT_NEAR(0, error, 1e-10);
  EXPECT_NEAR(0.25, Diff(sqrt, 4), 1e-7);
  EXPECT_NEAR(0, Diff(std::bind2nd(std::ptr_fun(gsl_hypot), -5), 0), 1e-7);
  EXPECT_NEAR(1, Diff(gsl_log1p, 0), 1e-7);
}

TEST_F(GSLTest, DiffPropagatesNaN) {
  EXPECT_TRUE(gsl_isnan(sqrt(-1)));
  EXPECT_TRUE(gsl_isnan(Diff(sqrt, -1)));
}

TEST_F(GSLTest, DiffDetectsNaN) {
  EXPECT_EQ(0, std::bind2nd(std::ptr_fun(gsl_hypot), 0)(0));
  EXPECT_TRUE(gsl_isnan(Diff(std::bind2nd(std::ptr_fun(gsl_hypot), 0), 0)));
  EXPECT_EQ(GSL_NEGINF, log(0));
  EXPECT_TRUE(gsl_isnan(Diff(log, 0)));
}

TEST_F(GSLTest, DiffRightDeriv) {
  // Diff should use the right derivative if the function is not defined for
  // negative argument.
  EXPECT_TRUE(gsl_isnan(gsl_sf_bessel_jl(0, -1e-7)));
  EXPECT_NEAR(0,
      Diff(std::bind1st(std::ptr_fun(gsl_sf_bessel_jl), 0), 0), 1e-7);
}

TEST_F(GSLTest, Derivative) {
  Derivative d(GetFunction("gsl_hypot"), 0, 1, ArgList(1, 0), 0);
  ASSERT_EQ(1, d(0));
  ASSERT_EQ(1 / sqrt(2), d(1));
  d = Derivative(GetFunction("gsl_hypot"), 1, 1, ArgList(1, 0), 0);
  ASSERT_EQ(0, d(0));
  ASSERT_EQ(1 / sqrt(2), d(1));
  EXPECT_THROW(
      Derivative(GetFunction("gsl_hypot"), 2, 0, ArgList(0, 0), 0),
      std::out_of_range);
  EXPECT_THROW(
      Derivative(GetFunction("gsl_hypot"), 0, 2, ArgList(0, 0), 0),
      std::out_of_range);
}

TEST_F(GSLTest, Dig) {
  EXPECT_TRUE(Dig(0, Dig::NO_VAR) == nullptr);
  EXPECT_EQ(1, Dig(0, 0)[0]);
  EXPECT_THROW(Dig(0, 1), std::out_of_range);
  EXPECT_TRUE(Dig(ArgList(0, 0, 0), Dig::NO_VAR) == nullptr);
  EXPECT_EQ(string("\1\0\0", 3), string(Dig(ArgList(0, 0, 0), 0), 3));
  EXPECT_EQ(string("\0\1\0", 3), string(Dig(ArgList(0, 0, 0), 1), 3));
  EXPECT_EQ(string("\0\0\1", 3), string(Dig(ArgList(0, 0, 0), 2), 3));
}

TEST_F(GSLTest, Elementary) {
  TEST_FUNC(log1p);
  TEST_FUNC(expm1);
  TEST_FUNC(hypot);
  TEST_FUNC(hypot3);
}

TEST_F(GSLTest, AiryA) {
  TEST_FUNC(sf_airy_Ai);
  TEST_FUNC(sf_airy_Ai_scaled);
}

TEST_F(GSLTest, AiryB) {
  TEST_FUNC(sf_airy_Bi);
  TEST_FUNC(sf_airy_Bi_scaled);
}

TEST_F(GSLTest, AiryZero) {
  TEST_FUNC(sf_airy_zero_Ai);
  TEST_FUNC(sf_airy_zero_Bi);
  TEST_FUNC(sf_airy_zero_Ai_deriv);
  TEST_FUNC(sf_airy_zero_Bi_deriv);
}

TEST_F(GSLTest, BesselJ) {
  TEST_FUNC(sf_bessel_J0);
  TEST_FUNC(sf_bessel_J1);
  TEST_FUNC(sf_bessel_Jn);
}

TEST_F(GSLTest, BesselY) {
  TEST_FUNC(sf_bessel_Y0);
  TEST_FUNC(sf_bessel_Y1);
  TEST_FUNC(sf_bessel_Yn);
}

TEST_F(GSLTest, BesselI) {
  TEST_FUNC(sf_bessel_I0);
  TEST_FUNC(sf_bessel_I1);
  TEST_FUNC(sf_bessel_In);
  TEST_FUNC(sf_bessel_I0_scaled);
  TEST_FUNC(sf_bessel_I1_scaled);
  TEST_FUNC(sf_bessel_In_scaled);
}

TEST_F(GSLTest, BesselK) {
  TEST_FUNC(sf_bessel_K0);
  TEST_FUNC(sf_bessel_K1);
  TEST_FUNC(sf_bessel_Kn);
  TEST_FUNC(sf_bessel_K0_scaled);
  TEST_FUNC(sf_bessel_K1_scaled);
  TEST_FUNC(sf_bessel_Kn_scaled);
}

TEST_F(GSLTest, Besselj) {
  TEST_FUNC(sf_bessel_j0);
  TEST_FUNC(sf_bessel_j1);
  TEST_FUNC(sf_bessel_j2);
  TEST_FUNC(sf_bessel_jl);
}

TEST_F(GSLTest, Bessely) {
  TEST_FUNC(sf_bessel_y0);
  TEST_FUNC(sf_bessel_y1);
  TEST_FUNC(sf_bessel_y2);
  TEST_FUNC(sf_bessel_yl);
}

TEST_F(GSLTest, Besseli) {
  TEST_FUNC(sf_bessel_i0_scaled);
  TEST_FUNC(sf_bessel_i1_scaled);
  TEST_FUNC(sf_bessel_i2_scaled);
  TEST_FUNC(sf_bessel_il_scaled);
}

TEST_F(GSLTest, Besselk) {
  TEST_FUNC(sf_bessel_k0_scaled);
  TEST_FUNC(sf_bessel_k1_scaled);
  TEST_FUNC(sf_bessel_k2_scaled);
  TEST_FUNC(sf_bessel_kl_scaled);
}

struct BesselFractionalOrderInfo : FunctionInfo {
  bool HasDerivative(unsigned var_index, const vector<real>& args) const {
    // Computing gsl_sf_bessel_*nu'(nu, x) requires
    // gsl_sf_bessel_*nu(nu - 1, x) which doesn't work when the
    // first argument is non-negative, so nu should be >= 1.
    // Partial derivatives with respect to nu are not provided.
    return var_index == 1 && args[0] >= 1;
  }

  bool HasDerivative2(const vector<real>& args) const {
    // Computing gsl_sf_bessel_*nu''(nu, x) requires
    // gsl_sf_bessel_*nu(nu - 2, x) which doesn't work when the
    // first argument is non-negative, so nu should be >= 2.
    return args[0] >= 2;
  }
};

TEST_F(GSLTest, BesselFractionalOrder) {
  BesselFractionalOrderInfo info;
  TEST_FUNC(sf_bessel_Jnu);
  TEST_FUNC(sf_bessel_Ynu);
  TEST_FUNC(sf_bessel_Inu);
  TEST_FUNC(sf_bessel_Inu_scaled);
  TEST_FUNC(sf_bessel_Knu);
  TEST_FUNC(sf_bessel_lnKnu);
  TEST_FUNC(sf_bessel_Knu_scaled);
}

TEST_F(GSLTest, BesselZero) {
  TEST_FUNC(sf_bessel_zero_J0);
  TEST_FUNC(sf_bessel_zero_J1);

  const char *name = "gsl_sf_bessel_zero_Jnu";
  Function af = GetFunction(name);
  for (size_t i = 0; i != NUM_POINTS; ++i) {
    for (size_t j = 0; j != NUM_POINTS; ++j) {
      double nu = POINTS[i], x = POINTS[j];
      ArgList args(nu, x);
      if (static_cast<unsigned>(x) != x) {
        af(args, ERROR);
        continue;
      }
      double value = gsl_sf_bessel_zero_Jnu(nu, x);
      if (gsl_isnan(value))
        af(args, ERROR);
      else
        EXPECT_EQ(value, af(args)) << name << " at " << x;
      af(args, DERIVS | ERROR);
    }
  }
}

TEST_F(GSLTest, Clausen) {
  TEST_FUNC(sf_clausen);
}

TEST_F(GSLTest, Hydrogenic) {
  TEST_FUNC(sf_hydrogenicR_1);

  const char *name = "gsl_sf_hydrogenicR";
  Function af = GetFunction(name);
  EXPECT_EQ(2, af(ArgList(1, 0, 1, 0)));
  EXPECT_STREQ("argument 'n' can't be represented as int, n = 1.1",
      af(ArgList(1.1, 0, 1, 0), PASS_ERROR).error());
  EXPECT_STREQ("argument 'l' can't be represented as int, l = 0.1",
      af(ArgList(1, 0.1, 1, 0), PASS_ERROR).error());
  for (size_t in = 0; in != NUM_POINTS_FOR_N; ++in) {
    int n = POINTS_FOR_N[in];
    if (n < -1000 || n > 1000) continue;
    for (size_t il = 0; il != NUM_POINTS_FOR_N; ++il) {
      int el = POINTS_FOR_N[il];
      if (el < -1000 || el > 1000) continue;
      for (size_t iz = 0; iz != NUM_POINTS; ++iz) {
        for (size_t ir = 0; ir != NUM_POINTS; ++ir) {
          double z = POINTS[iz], r = POINTS[ir];
          ArgList args(n, el, z, r);
          CheckFunction(gsl_sf_hydrogenicR(n, el, z, r), af, args);
          af(args, DERIVS | ERROR);
        }
      }
    }
  }
}

TEST_F(GSLTest, Coulomb) {
  const char *name = "gsl_sf_coulomb_CL";
  Function f = GetFunction(name);
  for (size_t i = 0; i != NUM_POINTS; ++i) {
    for (size_t j = 0; j != NUM_POINTS; ++j) {
      double x = POINTS[i], y = POINTS[j];
      ArgList args(x, y);
      gsl_sf_result result = {};
      double value = gsl_sf_coulomb_CL_e(x, y, &result) ? GSL_NAN : result.val;
      CheckFunction(value, f, args);
      f(args, DERIVS | ERROR);
      f(args, HES | ERROR);
    }
  }
}

TEST_F(GSLTest, Coupling3j) {
  double value = gsl_sf_coupling_3j(8, 20, 12, -2, 12, -10);
  EXPECT_NEAR(0.0812695955, value, 1e-5);
  Function af = GetFunction("gsl_sf_coupling_3j");
  EXPECT_EQ(value, af(ArgList(8, 20, 12, -2, 12, -10)));
  af(ArgList(0, 0, 0, 0, 0, 0));
  af(ArgList(0.5, 0, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0.5, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0.5, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0.5, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0.5, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0, 0.5), ERROR);
  af(ArgList(8, 20, 12, -2, 12, -10), ERROR | DERIVS);
}

TEST_F(GSLTest, Coupling6j) {
  double value = gsl_sf_coupling_6j(2, 4, 6, 8, 10, 12);
  EXPECT_NEAR(0.0176295295, value, 1e-7);
  Function af = GetFunction("gsl_sf_coupling_6j");
  EXPECT_EQ(value, af(ArgList(2, 4, 6, 8, 10, 12)));
  af(ArgList(0, 0, 0, 0, 0, 0));
  af(ArgList(0.5, 0, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0.5, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0.5, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0.5, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0.5, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0, 0.5), ERROR);
  af(ArgList(2, 4, 6, 8, 10, 12), ERROR | DERIVS);
}

TEST_F(GSLTest, Coupling9j) {
  double value = gsl_sf_coupling_9j(6, 16, 18, 8, 20, 14, 12, 10, 4);
  EXPECT_NEAR(-0.000775648399, value, 1e-9);
  Function af = GetFunction("gsl_sf_coupling_9j");
  EXPECT_EQ(value, af(ArgList(6, 16, 18, 8, 20, 14, 12, 10, 4)));
  af(ArgList(0, 0, 0, 0, 0, 0, 0, 0, 0));
  af(ArgList(0.5, 0, 0, 0, 0, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0.5, 0, 0, 0, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0.5, 0, 0, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0.5, 0, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0.5, 0, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0, 0.5, 0, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0, 0, 0.5, 0, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0, 0, 0, 0.5, 0), ERROR);
  af(ArgList(0, 0, 0, 0, 0, 0, 0, 0, 0.5), ERROR);
  af(ArgList(6, 16, 18, 8, 20, 14, 12, 10, 4), ERROR | DERIVS);
}

TEST_F(GSLTest, Dawson) {
  TEST_FUNC(sf_dawson);
}

TEST_F(GSLTest, Debye) {
  TEST_FUNC(sf_debye_1);
  TEST_FUNC(sf_debye_2);
  TEST_FUNC(sf_debye_3);
  TEST_FUNC(sf_debye_4);
  TEST_FUNC(sf_debye_5);
  TEST_FUNC(sf_debye_6);
}

struct DilogFunctionInfo : FunctionInfo {
  double GetDerivative(const vector<real> &args) const {
    return args[0] == 1 ? GSL_POSINF : GSL_NAN;
  }
};

TEST_F(GSLTest, Dilog) {
  DilogFunctionInfo info;
  TEST_FUNC(sf_dilog);
}

TEST_F(GSLTest, EllInt) {
  // TODO
  TEST_FUNC(sf_ellint_Kcomp);
  TEST_FUNC(sf_ellint_Ecomp);
  TEST_FUNC(sf_ellint_Pcomp);
}
}
