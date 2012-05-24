#include <ilconcert/ilomodel.h>

#include <algorithm>
#include <memory>
#include <sstream>

#include "gtest/gtest.h"

#include "solvers/concert/concert.h"
#include "solvers/asl.h"
#include "solvers/nlp.h"
#include "solvers/opcode.hd"
#include "tests/config.h"

using std::string;

namespace {

bool AreBothSpaces(char lhs, char rhs) { return lhs == ' ' && rhs == ' '; }

// Returns a string representation of the argument.
template <typename T>
string str(T t) {
  std::ostringstream ss;
  ss << t;
  string s = ss.str();

  // Replace adjacent duplicate spaces and possible trailing space.
  string::iterator end = std::unique(s.begin(), s.end(), AreBothSpaces);
  if (*(end - 1) == ' ') --end;
  s.erase(end, s.end());

  return s;
}

// A functor for deleting ASL expressions recursively.
struct ExprDeleter {
  void operator()(expr *e) const;
};

void ExprDeleter::operator()(expr *e) const {
  if (!e) return;
  size_t op = reinterpret_cast<size_t>(e->op);
  switch (op) {
  case OPNUM:
    delete reinterpret_cast<expr_n*>(e);
    return;
  case MINLIST: case MAXLIST: {
    expr_va *eva = reinterpret_cast<expr_va*>(e);
    for (de *d = reinterpret_cast<expr_va*>(e)->L.d; d->e; ++d)
      (*this)(d->e);
    delete eva;
    return;
  }
  case OPIFnl: {
    expr_if *eif = reinterpret_cast<expr_if*>(e);
    (*this)(eif->e);
    (*this)(eif->T);
    (*this)(eif->F);
    delete eif;
    return;
  }
  case OPSUMLIST:
    for (expr **i = e->L.ep, **end = e->R.ep; i < end; ++i)
      (*this)(*i);
    delete e;
    return;
  }
  // Delete subexpressions recursively.
  (*this)(e->L.e);
  (*this)(e->R.e);
  delete e;
}

#if HAVE_UNIQUE_PTR
typedef std::unique_ptr<expr, ExprDeleter> ExprPtr;
#else
// Fall back to std::auto_ptr - leaks subexpressions.
typedef std::auto_ptr<expr> ExprPtr;
ExprPtr move(ExprPtr e) { return e; }
#endif

class ConcertTest : public ::testing::Test {
 protected:
  void SetUp() {
    env = IloEnv();
    mod = IloModel(env);
    Var = IloNumVarArray(env, 3);
    Var[0] = IloNumVar(env, 0, 1, "x");
    Var[1] = IloNumVar(env, 0, 1, "y");
    Var[2] = IloNumVar(env, 0, 1, "theta");
  }

  void TearDown() {
    Var = IloNumVarArray();
  }

  // Creates an ASL expression representing a number.
  static ExprPtr NewNum(double n) {
    expr_n e = {reinterpret_cast<efunc_n*>(OPNUM), n};
    return ExprPtr(reinterpret_cast<expr*>(new expr_n(e)));
  }

  // Creates an ASL expression representing a variable.
  static ExprPtr NewVar(int var_index) {
    expr e = {reinterpret_cast<efunc*>(OPVARVAL), var_index, 0, {0}, {0}, 0};
    return ExprPtr(new expr(e));
  }

  // Creates an unary ASL expression.
  static ExprPtr NewUnary(int opcode, ExprPtr arg) {
    expr e = {reinterpret_cast<efunc*>(opcode), 0, 0,
              {arg.release()}, {0}, 0};
    return ExprPtr(new expr(e));
  }

  // Creates a binary ASL expression.
  static ExprPtr NewBinary(int opcode, ExprPtr lhs, ExprPtr rhs) {
    expr e = {reinterpret_cast<efunc*>(opcode), 0, 0,
              {lhs.release()}, {rhs.release()}, 0};
    return ExprPtr(new expr(e));
  }

  static de MakeDE(ExprPtr e) {
    de result = {e.release(), 0, {0}};
    return result;
  }

  // Creates a variable-argument ASL expression with 3 arguments.
  static ExprPtr NewExpr(int opcode, ExprPtr e1, ExprPtr e2, ExprPtr e3);

  // Creates an if ASL expression.
  static ExprPtr NewIf(ExprPtr condition,
      ExprPtr true_expr, ExprPtr false_expr);

  static double EvalRem(double lhs, double rhs) {
    IloExpr e(build_expr(NewBinary(OPREM, NewNum(lhs), NewNum(rhs)).get()));
    return e.getImpl()->eval(IloAlgorithm());
  }
};

ExprPtr ConcertTest::NewExpr(int opcode, ExprPtr e1, ExprPtr e2, ExprPtr e3) {
  expr_va e = {reinterpret_cast<efunc*>(opcode), 0,
               {0}, {0}, 0, 0, 0};
  expr_va *copy = new expr_va(e);
  ExprPtr result(reinterpret_cast<expr*>(copy));
  de *args = new de[4];
  args[0] = MakeDE(move(e1));
  args[1] = MakeDE(move(e2));
  args[2] = MakeDE(move(e3));
  args[3] = MakeDE(ExprPtr());
  copy->L.d = args;
  return result;
}

ExprPtr ConcertTest::NewIf(ExprPtr condition,
    ExprPtr true_expr, ExprPtr false_expr) {
  expr_if e = {reinterpret_cast<efunc*>(OPIFnl), 0, condition.release(),
               true_expr.release(), false_expr.release(),
               0, 0, 0, 0, {0}, {0}, 0, 0};
  return ExprPtr(reinterpret_cast<expr*>(new expr_if(e)));
}

TEST_F(ConcertTest, ConvertNum) {
  EXPECT_EQ("0.42", str(build_expr(NewNum(0.42).get())));
}

TEST_F(ConcertTest, ConvertVar) {
  EXPECT_EQ("theta", str(build_expr(NewVar(2).get())));
}

TEST_F(ConcertTest, ConvertPlus) {
  EXPECT_EQ("x + 42", str(build_expr(
    NewBinary(OPPLUS, NewVar(0), NewNum(42)).get())));
  EXPECT_EQ("x + y", str(build_expr(
    NewBinary(OPPLUS, NewVar(0), NewVar(1)).get())));
}

TEST_F(ConcertTest, ConvertMinus) {
  EXPECT_EQ("x + -42", str(build_expr(
    NewBinary(OPMINUS, NewVar(0), NewNum(42)).get())));
  EXPECT_EQ("x + -1 * y", str(build_expr(
    NewBinary(OPMINUS, NewVar(0), NewVar(1)).get())));
}

TEST_F(ConcertTest, ConvertMult) {
  EXPECT_EQ("42 * x", str(build_expr(
    NewBinary(OPMULT, NewVar(0), NewNum(42)).get())));
  EXPECT_EQ("x * y", str(build_expr(
    NewBinary(OPMULT, NewVar(0), NewVar(1)).get())));
}

TEST_F(ConcertTest, ConvertDiv) {
  EXPECT_EQ("x / 42", str(build_expr(
    NewBinary(OPDIV, NewVar(0), NewNum(42)).get())));
  EXPECT_EQ("x / y", str(build_expr(
    NewBinary(OPDIV, NewVar(0), NewVar(1)).get())));
}

TEST_F(ConcertTest, ConvertRem) {
  EXPECT_EQ("x + trunc(x / y ) * y * -1", str(build_expr(
    NewBinary(OPREM, NewVar(0), NewVar(1)).get())));
  EXPECT_EQ(0, EvalRem(9, 3));
  EXPECT_EQ(2, EvalRem(8, 3));
  EXPECT_EQ(-2, EvalRem(-8, 3));
  EXPECT_EQ(2, EvalRem(8, -3));
  EXPECT_EQ(-2, EvalRem(-8, -3));
  EXPECT_EQ(1.5, EvalRem(7.5, 3));
}

TEST_F(ConcertTest, ConvertPow) {
  EXPECT_EQ("x ^ 42", str(build_expr(
    NewBinary(OPPOW, NewVar(0), NewNum(42)).get())));
  EXPECT_EQ("x ^ y", str(build_expr(
    NewBinary(OPPOW, NewVar(0), NewVar(1)).get())));
}

TEST_F(ConcertTest, ConvertLess) {
  EXPECT_EQ("max(x + -42 , 0)", str(build_expr(
    NewBinary(OPLESS, NewVar(0), NewNum(42)).get())));
  EXPECT_EQ("max(x + -1 * y , 0)", str(build_expr(
    NewBinary(OPLESS, NewVar(0), NewVar(1)).get())));
}

TEST_F(ConcertTest, ConvertMin) {
  EXPECT_EQ("min( [x , y , 42 ])", str(build_expr(
    NewExpr(MINLIST, NewVar(0), NewVar(1), NewNum(42)).get())));
}

TEST_F(ConcertTest, ConvertMax) {
  EXPECT_EQ("max([x , y , 42 ])", str(build_expr(
    NewExpr(MAXLIST, NewVar(0), NewVar(1), NewNum(42)).get())));
}

TEST_F(ConcertTest, ConvertFloor) {
  EXPECT_EQ("floor(x )", str(build_expr(NewUnary(FLOOR, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertCeil) {
  EXPECT_EQ("ceil(x )", str(build_expr(NewUnary(CEIL, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertAbs) {
  EXPECT_EQ("abs(x )", str(build_expr(NewUnary(ABS, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertUMinus) {
  EXPECT_EQ("-1 * x", str(build_expr(NewUnary(OPUMINUS, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertLogicalOrComparisonThrows) {
  int ops[] = {OPOR, OPAND, LT, LE, EQ, GE, GT, NE, OPNOT};
  size_t i = 0;
  for (size_t num_ops = sizeof(ops) / sizeof(*ops); i < num_ops; ++i) {
    EXPECT_THROW(build_expr(
      NewBinary(ops[i], NewVar(0), NewVar(1)).get()), Error);
  }
  // Paranoid: make sure that the loop body has been executed enough times.
  EXPECT_EQ(9u, i);
}

TEST_F(ConcertTest, ConvertIf) {
  EXPECT_EQ("IloNumVar(7)[-inf..inf]", str(build_expr(NewIf(NewBinary(EQ,
    NewVar(0), NewNum(0)), NewVar(1), NewNum(42)).get())));

  IloModel::Iterator iter(mod);
  ASSERT_TRUE(iter.ok());
  IloIfThenI *ifTrue = dynamic_cast<IloIfThenI*>((*iter).getImpl());
  ASSERT_TRUE(ifTrue != nullptr);
  EXPECT_EQ("x == 0", str(ifTrue->getLeft()));
  EXPECT_EQ("IloNumVar(7)[-inf..inf] == y", str(ifTrue->getRight()));

  ++iter;
  ASSERT_TRUE(iter.ok());
  IloIfThenI *ifFalse = dynamic_cast<IloIfThenI*>((*iter).getImpl());
  ASSERT_TRUE(ifFalse != nullptr);
  IloNotI *ifNot = dynamic_cast<IloNotI*>(ifFalse->getLeft().getImpl());
  EXPECT_EQ("x == 0", str(ifNot->getConstraint()));
  EXPECT_EQ("IloNumVar(7)[-inf..inf] == 42", str(ifFalse->getRight()));

  ++iter;
  EXPECT_FALSE(iter.ok());
}

TEST_F(ConcertTest, ConvertTanh) {
  EXPECT_EQ("exp(2 * x ) + -1 / exp(2 * x ) + 1",
            str(build_expr(NewUnary(OP_tanh, NewVar(0)).get())));
  // Concert incorrectly omits brackets around the dividend and divisor
  // above, so test also by evaluating the expression at several points.
  IloExpr e(build_expr(NewUnary(OP_tanh, NewNum(1)).get()));
  EXPECT_NEAR(0.761594, e.getImpl()->eval(IloAlgorithm()), 1e-5);
  e = build_expr(NewUnary(OP_tanh, NewNum(0)).get());
  EXPECT_EQ(0, e.getImpl()->eval(IloAlgorithm()));
  e = build_expr(NewUnary(OP_tanh, NewNum(-2)).get());
  EXPECT_NEAR(-0.964027, e.getImpl()->eval(IloAlgorithm()), 1e-5);
}

TEST_F(ConcertTest, ConvertTan) {
  EXPECT_EQ("tan(x )", str(build_expr(NewUnary(OP_tan, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertSqrt) {
  EXPECT_EQ("x ^ 0.5",
            str(build_expr(NewUnary(OP_sqrt, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertSinh) {
  EXPECT_EQ("exp(x ) * 0.5 + exp(-1 * x ) * -0.5",
            str(build_expr(NewUnary(OP_sinh, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertSin) {
  EXPECT_EQ("sin(x )", str(build_expr(NewUnary(OP_sin, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertLog10) {
  EXPECT_EQ("log(x )/ 2.30259",
            str(build_expr(NewUnary(OP_log10, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertLog) {
  EXPECT_EQ("log(x )", str(build_expr(NewUnary(OP_log, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertExp) {
  EXPECT_EQ("exp(x )", str(build_expr(NewUnary(OP_exp, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertCosh) {
  EXPECT_EQ("exp(x ) * 0.5 + exp(-1 * x ) * 0.5",
            str(build_expr(NewUnary(OP_cosh, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertCos) {
  EXPECT_EQ("cos(x )", str(build_expr(NewUnary(OP_cos, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertAtanh) {
  EXPECT_EQ("log(x + 1 ) * 0.5 + log(-1 * x + 1 ) * -0.5",
            str(build_expr(NewUnary(OP_atanh, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertAtan2) {
  EXPECT_EQ("IloNumVar(8)[-inf..inf]",
            str(build_expr(NewBinary(OP_atan2, NewVar(1), NewVar(0)).get())));

  IloModel::Iterator iter(mod);
  ASSERT_TRUE(iter.ok());
  IloIfThenI *ifXNonnegative = dynamic_cast<IloIfThenI*>((*iter).getImpl());
  ASSERT_TRUE(ifXNonnegative != nullptr);
  EXPECT_EQ("0 <= x", str(ifXNonnegative->getLeft()));
  EXPECT_EQ("IloNumVar(8)[-inf..inf] == arc-tan(y / x )", // (1)
            str(ifXNonnegative->getRight()));

  ++iter;
  ASSERT_TRUE(iter.ok());
  IloIfThenI *ifDiffSigns = dynamic_cast<IloIfThenI*>((*iter).getImpl());
  ASSERT_TRUE(ifDiffSigns != nullptr);
  EXPECT_EQ("(x <= 0 ) && (0 <= y )", str(ifDiffSigns->getLeft()));
  EXPECT_EQ("IloNumVar(8)[-inf..inf] == arc-tan(y / x ) + 3.14159", // (2)
            str(ifDiffSigns->getRight()));

  ++iter;
  ASSERT_TRUE(iter.ok());
  IloIfThenI *ifSameSigns = dynamic_cast<IloIfThenI*>((*iter).getImpl());
  ASSERT_TRUE(ifSameSigns != nullptr);
  EXPECT_EQ("(x <= 0 ) && (y <= 0 )", str(ifSameSigns->getLeft()));
  EXPECT_EQ("IloNumVar(8)[-inf..inf] == arc-tan(y / x ) + -3.14159",
            str(ifSameSigns->getRight()));

  ++iter;
  EXPECT_FALSE(iter.ok());

  // Check that (1) and (2) both yield NaN when x == 0 and y == 0.
  double d = IloArcTan(0.0 / 0.0);
  EXPECT_TRUE(d != d);
  double d1 = d + 3.14;
  EXPECT_TRUE(d1 != d1);
}

TEST_F(ConcertTest, ConvertAtan) {
  EXPECT_EQ("arc-tan(x )",
            str(build_expr(NewUnary(OP_atan, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertAsinh) {
  EXPECT_EQ("log(x + square(x ) + 1 ^ 0.5)",
            str(build_expr(NewUnary(OP_asinh, NewVar(0)).get())));
  // Concert incorrectly omits brackets around square(x) + 1
  // above, so test also by evaluating the expression at several points.
  IloExpr e(build_expr(NewUnary(OP_asinh, NewNum(1)).get()));
  EXPECT_NEAR(0.881373, e.getImpl()->eval(IloAlgorithm()), 1e-5);
  e = build_expr(NewUnary(OP_asinh, NewNum(0)).get());
  EXPECT_EQ(0, e.getImpl()->eval(IloAlgorithm()));
  e = build_expr(NewUnary(OP_asinh, NewNum(-2)).get());
  EXPECT_NEAR(-1.443635, e.getImpl()->eval(IloAlgorithm()), 1e-5);
}

TEST_F(ConcertTest, ConvertAsin) {
  EXPECT_EQ("arc-sin(x )",
            str(build_expr(NewUnary(OP_asin, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertAcosh) {
  EXPECT_EQ("log(x + x + 1 ^ 0.5 * x + -1 ^ 0.5)",
            str(build_expr(NewUnary(OP_acosh, NewVar(0)).get())));
  // Concert incorrectly omits brackets around x + 1 and x + -1
  // above, so test also by evaluating the expression at several points.
  IloExpr e(build_expr(NewUnary(OP_acosh, NewNum(1)).get()));
  EXPECT_NEAR(0, e.getImpl()->eval(IloAlgorithm()), 1e-5);
  e = build_expr(NewUnary(OP_acosh, NewNum(10)).get());
  EXPECT_NEAR(2.993222, e.getImpl()->eval(IloAlgorithm()), 1e-5);
  e = build_expr(NewUnary(OP_acosh, NewNum(0)).get());
  double n = e.getImpl()->eval(IloAlgorithm());
  EXPECT_TRUE(n != n);
}

TEST_F(ConcertTest, ConvertAcos) {
  EXPECT_EQ("arc-cos(x )",
            str(build_expr(NewUnary(OP_acos, NewVar(0)).get())));
}

TEST_F(ConcertTest, ConvertSum) {
  expr e = {reinterpret_cast<efunc*>(OPSUMLIST), 0, 0, {0}, {0}, 0};
  ExprPtr sum(new expr(e));
  expr** args = sum->L.ep = new expr*[3];
  sum->R.ep = args + 3;
  ExprPtr x(NewVar(0)), y(NewVar(1)), n(NewNum(42));
  args[0] = x.release();
  args[1] = y.release();
  args[2] = n.release();
  EXPECT_EQ("x + y + 42", str(build_expr(sum.get())));
}

TEST_F(ConcertTest, ConvertIntDiv) {
  EXPECT_EQ("trunc(x / y )", str(build_expr(
    NewBinary(OPintDIV, NewVar(0), NewVar(1)).get())));
}
}
