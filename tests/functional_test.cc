// Tests for the functional stuff.

#include <limits>
#include <sstream>
#include <cmath>

#include "gtest/gtest.h"
#include "tests/functional.h"

using std::ptr_fun;
using std::sqrt;
using std::vector;
using fun::Tuple;
using fun::Differentiator;

namespace {

const double ARGS[] = {5, 7, 11, 13, 17, 19, 23, 29, 31};

void CheckTuple(unsigned size, const Tuple &t) {
  EXPECT_EQ(size, t.size());
  Tuple copy(t);
  for (unsigned i = 0; i < size; ++i) {
    EXPECT_EQ(ARGS[i], t[i]);
    EXPECT_EQ(ARGS[i], copy[i]);
    copy[i] = 42;
    EXPECT_EQ(42, copy[i]);
  }
}

TEST(FunctionalTest, Tuple) {
  CheckTuple(1, Tuple(5));
  CheckTuple(2, Tuple(5, 7));
  CheckTuple(3, Tuple(5, 7, 11));
  CheckTuple(4, Tuple(5, 7, 11, 13));
  CheckTuple(5, Tuple(5, 7, 11, 13, 17));
  CheckTuple(6, Tuple(5, 7, 11, 13, 17, 19));
  CheckTuple(9, Tuple(5, 7, 11, 13, 17, 19, 23, 29, 31));
}

TEST(FunctionalTest, TupleOutput) {
  std::ostringstream os;
  os << Tuple(42);
  EXPECT_EQ("(42)", os.str());
  os.str("");
  os << Tuple(3, 5, 7);
  EXPECT_EQ("(3, 5, 7)", os.str());
}

#define EXPECT_TYPE(expected, actual) \
  { expected *e = static_cast<actual*>(0); e = e; }

TEST(FunctionalTest, TernaryFunction) {
  typedef fun::ternary_function<bool, char, int, double> Fun;
  EXPECT_TYPE(bool, Fun::first_argument_type);
  EXPECT_TYPE(char, Fun::second_argument_type);
  EXPECT_TYPE(int, Fun::third_argument_type);
  EXPECT_TYPE(double, Fun::result_type);
}

double Ternary(bool b, char c, int i) { return b + c + i; }

TEST(FunctionalTest, PointerToTernaryFunction) {
  typedef fun::pointer_to_ternary_function<bool, char, int, double> Fun;
  EXPECT_TYPE(bool, Fun::first_argument_type);
  EXPECT_TYPE(char, Fun::second_argument_type);
  EXPECT_TYPE(int, Fun::third_argument_type);
  EXPECT_TYPE(double, Fun::result_type);
  Fun f(Ternary);
  EXPECT_EQ(true + 'a' + 42, f(true, 'a', 42));
}

TEST(FunctionalTest, Fun) {
  Differentiator dx, dy;
  EXPECT_NEAR(4.77259,
      dx([&](double x) { return dy(bind1st(ptr_fun(pow), x), 2); }, 2),
      1e-5);
}

// TODO: test binders

double hypot(double x, double y) {
  return sqrt(x * x + y * y);
}

typedef double (*DoubleFun)(double);
DoubleFun GetDoubleFun(DoubleFun f) { return f; }

TEST(FunctionalTest, Differentiator) {
  Differentiator diff;
  double error = std::numeric_limits<double>::quiet_NaN();
  EXPECT_NEAR(1, diff(GetDoubleFun(std::sin), 0, &error), 1e-7);
  EXPECT_NEAR(0, error, 1e-10);
  EXPECT_NEAR(0.25, diff(GetDoubleFun(sqrt), 4), 1e-7);
  EXPECT_NEAR(0, diff(std::bind2nd(ptr_fun(hypot), -5), 0), 1e-7);
}

TEST(FunctionalTest, DifferentiatorPropagatesNaN) {
  Differentiator diff;
  EXPECT_TRUE(std::isnan(sqrt(-1)));
  EXPECT_TRUE(std::isnan(diff(GetDoubleFun(sqrt), -1)));
}

TEST(FunctionalTest, DifferentiatorDetectsNaN) {
  Differentiator diff;
  EXPECT_EQ(0, std::bind2nd(ptr_fun(hypot), 0)(0));
  EXPECT_TRUE(std::isnan(diff(std::bind2nd(ptr_fun(hypot), 0), 0)));
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), std::log(0));
  EXPECT_TRUE(std::isnan(diff(GetDoubleFun(std::log), 0)));
}

double PositiveOrNaN(double x) {
  return x >= 0 ? x : std::numeric_limits<double>::quiet_NaN();
}

TEST(FunctionalTest,DifferentiatorRightDeriv) {
  // Differentiator should use the right derivative if the function is not
  // defined for a negative argument.
  Differentiator diff;
  EXPECT_TRUE(std::isnan(PositiveOrNaN(-1e-7)));
  EXPECT_NEAR(1, diff(ptr_fun(PositiveOrNaN), 0), 1e-7);
}
}
