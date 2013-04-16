/*
 Solver test suite.

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

#include "tests/solver_test.h"
#include <cmath>
#include <math.h>

#define _USE_MATH_DEFINES

using ampl::LogicalExpr;
using ampl::NumericExpr;
using ampl::Problem;
using ampl::UnsupportedExprError;

// TODO: pass to the SolverTest information about the unsupported expressions

SolverTest::EvalResult SolverTest::Solve(Problem &p) {
  struct TestSolutionHandler : ampl::SolutionHandler {
    EvalResult result;
    virtual ~TestSolutionHandler() {}
    void HandleSolution(ampl::BasicSolver &, fmt::StringRef,
          const double *values, const double *, double obj_value) {
      if (values)
        result = EvalResult(values[0], obj_value);
    }
  };
  TestSolutionHandler sh;
  solver_->set_solution_handler(&sh);
  solver_->Solve(p);
  return sh.result;
}

SolverTest::EvalResult SolverTest::Solve(
    LogicalExpr e, int var1, int var2, int var3, bool need_result) {
  Problem p;
  p.AddVar(need_result ? negInfinity : 0,
      need_result ? Infinity : 0, ampl::INTEGER);
  p.AddVar(var1, var1, ampl::INTEGER);
  p.AddVar(var2, var2, ampl::INTEGER);
  p.AddVar(var3, var3, ampl::INTEGER);
  p.AddCon(e);
  return Solve(p);
}

SolverTest::SolverTest()
: solver_(GetParam()()), x(AddVar(1)), y(AddVar(2)), z(AddVar(3)) {}

TEST_P(SolverTest, Plus) {
  NumericExpr e = AddBinary(OPPLUS, x, y);
  EXPECT_EQ(25, Eval(e, 10, 15));
  EXPECT_EQ(12, Eval(e, 19, -7));
}

TEST_P(SolverTest, Minus) {
  NumericExpr e = AddBinary(OPMINUS, x, y);
  EXPECT_EQ(-5, Eval(e, 10, 15));
  EXPECT_EQ(26, Eval(e, 19, -7));
}

TEST_P(SolverTest, Mult) {
  NumericExpr e = AddBinary(OPMULT, x, y);
  EXPECT_EQ(150, Eval(e, 10, 15));
  EXPECT_EQ(-133, Eval(e, 19, -7));
}

TEST_P(SolverTest, Div) {
  try {
    NumericExpr e = AddBinary(OPDIV, x, y);
    Eval(e, 4, 2);  // May throw UnsupportedExprError.
    EXPECT_EQ(10, Eval(e, 150, 15));
    EXPECT_EQ(-7, Eval(e, -133, 19));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Rem) {
  NumericExpr e = AddBinary(OPREM, x, y);
  EXPECT_EQ(0, Eval(e, 9, 3));
  EXPECT_EQ(2, Eval(e, 8, 3));
  EXPECT_EQ(-2, Eval(e, -8, 3));
  EXPECT_EQ(2, Eval(e, 8, -3));
  EXPECT_EQ(-2, Eval(e, -8, -3));
}

TEST_P(SolverTest, Pow) {
  try {
    NumericExpr e = AddBinary(OPPOW, x, y);
    Eval(e, 2, 3);  // May throw UnsupportedExprError.
    EXPECT_EQ(8, Eval(e, 2, 3));
    EXPECT_EQ(81, Eval(e, 3, 4));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, NumericLess) {
  NumericExpr e = AddBinary(OPLESS, x, y);
  EXPECT_EQ(0, Eval(e, 10, 15));
  EXPECT_EQ(26, Eval(e, 19, -7));
}

TEST_P(SolverTest, Min) {
  NumericExpr e = AddVarArg(MINLIST, x, y, z);
  EXPECT_EQ(-7, Eval(e, 3, -7, 5));
  EXPECT_EQ(10, Eval(e, 10, 20, 30));
}

TEST_P(SolverTest, Max) {
  NumericExpr e = AddVarArg(MAXLIST, x, y, z);
  EXPECT_EQ(5, Eval(e, 3, -7, 5));
  EXPECT_EQ(30, Eval(e, 30, 20, 10));
}

TEST_P(SolverTest, Floor) {
  NumericExpr e = AddUnary(FLOOR, x);
  EXPECT_EQ(-42, Eval(e, -42));
  EXPECT_EQ(42, Eval(e, 42));
  try {
    Eval(AddNum(1.2));  // May throw UnsupportedExprError.
    EXPECT_EQ(4, Eval(AddUnary(FLOOR, AddNum(4.9))));
    EXPECT_EQ(-5, Eval(AddUnary(FLOOR, AddNum(-4.1))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Ceil) {
  NumericExpr e = AddUnary(CEIL, x);
  EXPECT_EQ(-42, Eval(e, -42));
  EXPECT_EQ(42, Eval(e, 42));
  try {
    Eval(AddNum(1.2));  // May throw UnsupportedExprError.
    EXPECT_EQ(5, Eval(AddUnary(CEIL, AddNum(4.1))));
    EXPECT_EQ(-4, Eval(AddUnary(CEIL, AddNum(-4.9))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Abs) {
  NumericExpr e = AddUnary(ABS, x);
  EXPECT_EQ(42, Eval(e, -42));
  EXPECT_EQ(42, Eval(e, 42));
}

TEST_P(SolverTest, UnaryMinus) {
  NumericExpr e = AddUnary(OPUMINUS, x);
  EXPECT_EQ(42, Eval(e, -42));
  EXPECT_EQ(-42, Eval(e, 42));
}

TEST_P(SolverTest, If) {
  NumericExpr e = AddIf(AddRelational(EQ, x, AddNum(1)), y, z);
  EXPECT_EQ(42, Eval(e, 1, 42, 10));
  EXPECT_EQ(10, Eval(e, 0, 42, 10));
}

TEST_P(SolverTest, Tanh) {
  try {
    EXPECT_EQ(1, Eval(AddBinary(OPMULT, AddNum(2),
        AddUnary(OP_tanh, AddNum((std::log(1.5) - std::log(0.5)) / 2)))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Tan) {
  try {
    EXPECT_EQ(42, Eval(AddUnary(OP_tan, AddNum(std::atan(42.0)))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Sqrt) {
  try {
    EXPECT_EQ(8, Eval(AddUnary(OP_sqrt, x), 64));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Sinh) {
  try {
    EXPECT_EQ(2, Eval(AddUnary(OP_sinh, AddNum(std::log(2 + std::sqrt(5.0))))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Sin) {
  try {
    EXPECT_EQ(1, Eval(AddUnary(OP_sin, AddNum(M_PI_2))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Log10) {
  try {
    EXPECT_EQ(3, Eval(AddUnary(OP_log10, AddNum(1000))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Log) {
  try {
    EXPECT_EQ(5, Eval(AddUnary(OP_log, AddNum(std::pow(M_E, 5)))));
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, Exp) {
  EXPECT_THROW(Eval(AddUnary(OP_exp, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Cosh) {
  EXPECT_THROW(Eval(AddUnary(OP_cosh, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Cos) {
  EXPECT_THROW(Eval(AddUnary(OP_cos, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Atanh) {
  EXPECT_THROW(Eval(AddUnary(OP_atanh, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Atan2) {
  EXPECT_THROW(Eval(AddBinary(OP_atan2, x, y)), UnsupportedExprError);
}

TEST_P(SolverTest, Atan) {
  EXPECT_THROW(Eval(AddUnary(OP_atan, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Asinh) {
  EXPECT_THROW(Eval(AddUnary(OP_asinh, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Asin) {
  EXPECT_THROW(Eval(AddUnary(OP_asin, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Acosh) {
  EXPECT_THROW(Eval(AddUnary(OP_acosh, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Acos) {
  EXPECT_THROW(Eval(AddUnary(OP_acos, x)), UnsupportedExprError);
}

TEST_P(SolverTest, Sum) {
  EXPECT_EQ(0, Eval(AddSum()));
  EXPECT_EQ(42, Eval(AddSum(x), 42));
  EXPECT_EQ(123, Eval(AddSum(x, y, z), 100, 20, 3));
}

TEST_P(SolverTest, IntDiv) {
  NumericExpr e = AddBinary(OPintDIV, x, y);
  EXPECT_EQ(3, Eval(e, 9, 3));
  EXPECT_EQ(2, Eval(e, 8, 3));
  EXPECT_EQ(-2, Eval(e, -8, 3));
  EXPECT_EQ(-2, Eval(e, 8, -3));
  EXPECT_EQ(2, Eval(e, -8, -3));
}

TEST_P(SolverTest, Precision) {
  EXPECT_THROW(Eval(AddBinary(OPprecision, x, y)), UnsupportedExprError);
}

TEST_P(SolverTest, Round) {
  EXPECT_EQ(42, Eval(AddBinary(OPround, x, AddNum(0)), 42));
  EXPECT_THROW(Eval(AddBinary(OPround, x, AddNum(1))), UnsupportedExprError);
  EXPECT_THROW(Eval(AddBinary(OPround, x, y)), UnsupportedExprError);
}

TEST_P(SolverTest, Trunc) {
  EXPECT_EQ(42, Eval(AddBinary(OPtrunc, x, AddNum(0)), 42));
  EXPECT_THROW(Eval(AddBinary(OPtrunc, x, AddNum(1))), UnsupportedExprError);
  EXPECT_THROW(Eval(AddBinary(OPtrunc, x, y)), UnsupportedExprError);
}

TEST_P(SolverTest, Count) {
  LogicalExpr a(AddRelational(NE, x, AddNum(0)));
  LogicalExpr b(AddRelational(NE, y, AddNum(0)));
  LogicalExpr c(AddRelational(NE, z, AddNum(0)));
  EXPECT_EQ(0, Eval(AddCount(a, b, c)));
  EXPECT_EQ(1, Eval(AddCount(a, b, c), 1));
  EXPECT_EQ(2, Eval(AddCount(a, b, c), 0, 1, 1));
  EXPECT_EQ(3, Eval(AddCount(a, b, c), 1, 1, 1));
}

TEST_P(SolverTest, NumberOf) {
  ampl::NumericConstant val = AddNum(42);
  EXPECT_EQ(0, Eval(AddNumberOf(val, x)));
  EXPECT_EQ(1, Eval(AddNumberOf(val, x), 42));
  EXPECT_EQ(0, Eval(AddNumberOf(val, x, y)));
  EXPECT_EQ(1, Eval(AddNumberOf(val, x, y), 0, 42));
  EXPECT_EQ(2, Eval(AddNumberOf(val, x, y), 42, 42));
  EXPECT_EQ(3, Eval(AddBinary(OPPLUS,
      AddNumberOf(val, x, y), AddNumberOf(AddNum(11), y, z)), 42, 42, 11));
}

TEST_P(SolverTest, PLTerm) {
  double args[] = {-1, 5, 0, 10, 1};
  EXPECT_THROW(Eval(AddPLTerm(5, args, 1), 0), UnsupportedExprError);
}

TEST_P(SolverTest, PowConstExp) {
  EXPECT_EQ(16, Eval(AddBinary(OP1POW, x, AddNum(4)), 2));
}

TEST_P(SolverTest, Pow2) {
  EXPECT_EQ(49, Eval(AddUnary(OP2POW, x), 7));
}

TEST_P(SolverTest, PowConstBase) {
  try {
    EvalResult result = Eval(AddBinary(OPCPOW, AddNum(5), x), 3);
    EXPECT_EQ(125, result);
  } catch (const UnsupportedExprError &) {
    // Ignore if not supported.
  }
}

TEST_P(SolverTest, NumericConstant) {
  EXPECT_EQ(42, Eval(AddNum(42)));
  std::string message;
  try {
    Eval(AddNum(0.42));
  } catch (const ampl::Error &e) {
    message = e.what();
  }
  EXPECT_EQ("value 0.42 can't be represented as int", message);
  EXPECT_EQ(solver_->var_min(), Eval(AddNum(solver_->var_min())));
  EXPECT_THROW(Eval(AddNum(solver_->var_min() - 1)), std::exception);
  EXPECT_EQ(solver_->var_max(), Eval(AddNum(solver_->var_max())));
  EXPECT_THROW(Eval(AddNum(solver_->var_max() + 1)), std::exception);
}

TEST_P(SolverTest, Var) {
  EXPECT_EQ(11, Eval(x, 11, 22));
  EXPECT_EQ(22, Eval(y, 11, 22));
  EXPECT_EQ(33, Eval(x, 33));
}

TEST_P(SolverTest, Or) {
  NumericExpr one = AddNum(1);
  LogicalExpr e = AddBinaryLogical(
      OPOR, AddRelational(EQ, x, one), AddRelational(EQ, y, one));
  EXPECT_EQ(0, Eval(e, 0, 0));
  EXPECT_EQ(1, Eval(e, 0, 1));
  EXPECT_EQ(1, Eval(e, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 1));
}

TEST_P(SolverTest, And) {
  NumericExpr one = AddNum(1);
  LogicalExpr e = AddBinaryLogical(
      OPAND, AddRelational(EQ, x, one), AddRelational(EQ, y, one));
  EXPECT_EQ(0, Eval(e, 0, 0));
  EXPECT_EQ(0, Eval(e, 0, 1));
  EXPECT_EQ(0, Eval(e, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 1));
}

TEST_P(SolverTest, Less) {
  LogicalExpr e = AddRelational(LT, x, y);
  EXPECT_EQ(0, Eval(e, 3, 3));
  EXPECT_EQ(1, Eval(e, 3, 5));
  EXPECT_EQ(0, Eval(e, 5, 3));
}

TEST_P(SolverTest, LessEqual) {
  LogicalExpr e = AddRelational(LE, x, y);
  EXPECT_EQ(1, Eval(e, 3, 3));
  EXPECT_EQ(1, Eval(e, 3, 5));
  EXPECT_EQ(0, Eval(e, 5, 3));
}

TEST_P(SolverTest, Equal) {
  LogicalExpr e = AddRelational(EQ, x, y);
  EXPECT_EQ(1, Eval(e, 3, 3));
  EXPECT_EQ(0, Eval(e, 3, 5));
  EXPECT_EQ(0, Eval(e, 5, 3));
}

TEST_P(SolverTest, GreaterEqual) {
  LogicalExpr e = AddRelational(GE, x, y);
  EXPECT_EQ(1, Eval(e, 3, 3));
  EXPECT_EQ(0, Eval(e, 3, 5));
  EXPECT_EQ(1, Eval(e, 5, 3));
}

TEST_P(SolverTest, Greater) {
  LogicalExpr e = AddRelational(GT, x, y);
  EXPECT_EQ(0, Eval(e, 3, 3));
  EXPECT_EQ(0, Eval(e, 3, 5));
  EXPECT_EQ(1, Eval(e, 5, 3));
}

TEST_P(SolverTest, NotEqual) {
  LogicalExpr e = AddRelational(NE, x, y);
  EXPECT_EQ(0, Eval(e, 3, 3));
  EXPECT_EQ(1, Eval(e, 3, 5));
  EXPECT_EQ(1, Eval(e, 5, 3));
}

TEST_P(SolverTest, Not) {
  LogicalExpr e = AddNot(AddRelational(EQ, x, AddNum(1)));
  EXPECT_EQ(1, Eval(e, 0));
  EXPECT_EQ(0, Eval(e, 1));
}

TEST_P(SolverTest, AtLeast) {
  LogicalExpr a(AddRelational(NE, y, AddNum(0)));
  LogicalExpr b(AddRelational(NE, z, AddNum(0)));
  LogicalExpr e = AddLogicalCount(OPATLEAST, AddVar(1), AddCount(a, b));
  EXPECT_EQ(1, Eval(e, 0, 0, 0));
  EXPECT_EQ(1, Eval(e, 0, 1, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 0));
  EXPECT_EQ(1, Eval(e, 1, 0, 1));
  EXPECT_EQ(1, Eval(e, 1, 1, 1));
  EXPECT_EQ(0, Eval(e, 2, 0, 1));
  EXPECT_EQ(1, Eval(e, 2, 1, 1));
}

TEST_P(SolverTest, AtMost) {
  LogicalExpr a(AddRelational(NE, y, AddNum(0)));
  LogicalExpr b(AddRelational(NE, z, AddNum(0)));
  LogicalExpr e = AddLogicalCount(OPATMOST, AddVar(1), AddCount(a, b));
  EXPECT_EQ(1, Eval(e, 0, 0, 0));
  EXPECT_EQ(0, Eval(e, 0, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 0, 0));
  EXPECT_EQ(1, Eval(e, 1, 0, 1));
  EXPECT_EQ(0, Eval(e, 1, 1, 1));
  EXPECT_EQ(1, Eval(e, 2, 0, 1));
  EXPECT_EQ(1, Eval(e, 2, 1, 1));
}

TEST_P(SolverTest, Exactly) {
  LogicalExpr a(AddRelational(NE, y, AddNum(0)));
  LogicalExpr b(AddRelational(NE, z, AddNum(0)));
  LogicalExpr e = AddLogicalCount(OPEXACTLY, AddVar(1), AddCount(a, b));
  EXPECT_EQ(1, Eval(e, 0, 0, 0));
  EXPECT_EQ(0, Eval(e, 0, 1, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 0));
  EXPECT_EQ(1, Eval(e, 1, 0, 1));
  EXPECT_EQ(0, Eval(e, 1, 1, 1));
  EXPECT_EQ(0, Eval(e, 2, 0, 1));
  EXPECT_EQ(1, Eval(e, 2, 1, 1));
}

TEST_P(SolverTest, NotAtLeast) {
  LogicalExpr a(AddRelational(NE, y, AddNum(0)));
  LogicalExpr b(AddRelational(NE, z, AddNum(0)));
  LogicalExpr e = AddLogicalCount(OPNOTATLEAST, AddVar(1), AddCount(a, b));
  EXPECT_EQ(0, Eval(e, 0, 0, 0));
  EXPECT_EQ(0, Eval(e, 0, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 0, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 1));
  EXPECT_EQ(0, Eval(e, 1, 1, 1));
  EXPECT_EQ(1, Eval(e, 2, 0, 1));
  EXPECT_EQ(0, Eval(e, 2, 1, 1));
}

TEST_P(SolverTest, NotAtMost) {
  LogicalExpr a(AddRelational(NE, y, AddNum(0)));
  LogicalExpr b(AddRelational(NE, z, AddNum(0)));
  LogicalExpr e = AddLogicalCount(OPNOTATMOST, AddVar(1), AddCount(a, b));
  EXPECT_EQ(0, Eval(e, 0, 0, 0));
  EXPECT_EQ(1, Eval(e, 0, 1, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 1));
  EXPECT_EQ(1, Eval(e, 1, 1, 1));
  EXPECT_EQ(0, Eval(e, 2, 0, 1));
  EXPECT_EQ(0, Eval(e, 2, 1, 1));
}

TEST_P(SolverTest, NotExactly) {
  LogicalExpr a(AddRelational(NE, y, AddNum(0)));
  LogicalExpr b(AddRelational(NE, z, AddNum(0)));
  LogicalExpr e = AddLogicalCount(OPNOTEXACTLY, AddVar(1), AddCount(a, b));
  EXPECT_EQ(0, Eval(e, 0, 0, 0));
  EXPECT_EQ(1, Eval(e, 0, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 0, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 1));
  EXPECT_EQ(1, Eval(e, 1, 1, 1));
  EXPECT_EQ(1, Eval(e, 2, 0, 1));
  EXPECT_EQ(0, Eval(e, 2, 1, 1));
}

TEST_P(SolverTest, ForAll) {
  LogicalExpr e = AddIteratedLogical(ANDLIST,
      AddRelational(EQ, x, AddNum(1)),
      AddRelational(EQ, y, AddNum(1)),
      AddRelational(EQ, z, AddNum(1)));
  EXPECT_EQ(0, Eval(e, 0, 0, 0));
  EXPECT_EQ(0, Eval(e, 0, 0, 1));
  EXPECT_EQ(0, Eval(e, 0, 1, 0));
  EXPECT_EQ(0, Eval(e, 0, 1, 1));
  EXPECT_EQ(0, Eval(e, 1, 0, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 1));
  EXPECT_EQ(0, Eval(e, 1, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 1, 1));
}

TEST_P(SolverTest, Exists) {
  LogicalExpr e = AddIteratedLogical(ORLIST,
      AddRelational(EQ, x, AddNum(1)),
      AddRelational(EQ, y, AddNum(1)),
      AddRelational(EQ, z, AddNum(1)));
  EXPECT_EQ(0, Eval(e, 0, 0, 0));
  EXPECT_EQ(1, Eval(e, 0, 0, 1));
  EXPECT_EQ(1, Eval(e, 0, 1, 0));
  EXPECT_EQ(1, Eval(e, 0, 1, 1));
  EXPECT_EQ(1, Eval(e, 1, 0, 0));
  EXPECT_EQ(1, Eval(e, 1, 0, 1));
  EXPECT_EQ(1, Eval(e, 1, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 1, 1));
}

TEST_P(SolverTest, Implication) {
  LogicalExpr e = AddImplication(
      AddRelational(EQ, x, AddNum(1)),
      AddRelational(EQ, y, AddNum(1)),
      AddRelational(EQ, z, AddNum(1)));
  EXPECT_EQ(0, Eval(e, 0, 0, 0));
  EXPECT_EQ(1, Eval(e, 0, 0, 1));
  EXPECT_EQ(0, Eval(e, 0, 1, 0));
  EXPECT_EQ(1, Eval(e, 0, 1, 1));
  EXPECT_EQ(0, Eval(e, 1, 0, 0));
  EXPECT_EQ(0, Eval(e, 1, 0, 1));
  EXPECT_EQ(1, Eval(e, 1, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 1, 1));
}

TEST_P(SolverTest, Iff) {
  LogicalExpr e = AddBinaryLogical(OP_IFF,
      AddRelational(EQ, x, AddNum(1)),
      AddRelational(EQ, y, AddNum(1)));
  EXPECT_EQ(1, Eval(e, 0, 0));
  EXPECT_EQ(0, Eval(e, 0, 1));
  EXPECT_EQ(0, Eval(e, 1, 0));
  EXPECT_EQ(1, Eval(e, 1, 1));
}

TEST_P(SolverTest, AllDiff) {
  LogicalExpr e = AddAllDiff(AddNum(1), x, y);
  EXPECT_TRUE(Solve(e, 2, 3).has_value());
  EXPECT_FALSE(Solve(e, 2, 1).has_value());
  EXPECT_FALSE(Solve(e, 1, 1).has_value());
}

TEST_P(SolverTest, NestedAllDiff) {
  EXPECT_THROW(Eval(AddNot(AddAllDiff(AddNum(1), x, y)), 1, 2),
      UnsupportedExprError);
}

TEST_P(SolverTest, LogicalConstant) {
  EXPECT_EQ(0, Eval(AddBool(false)));
  EXPECT_EQ(1, Eval(AddBool(true)));
}

TEST_P(SolverTest, NonlinearObj) {
  Problem p;
  p.AddVar(2, 2, ampl::INTEGER);
  ampl::Variable x = AddVar(0);
  p.AddObj(ampl::MIN, AddBinary(OPMULT, x, x));
  EXPECT_EQ(4, Solve(p).obj_value());
}
