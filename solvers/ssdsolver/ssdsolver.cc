/*
 AMPL solver for problems with second-order stochastic dominance (SSD)
 constraints.

 Copyright (C) 2013 AMPL Optimization LLC

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

#include "ssdsolver.h"

namespace {

struct ValueScenario {
  double value;
  int scenario;
};

struct ValueLess {
  bool operator()(const ValueScenario &lhs, const ValueScenario &rhs) const {
    return lhs.value < rhs.value;
  }
};
}

namespace ampl {

void SSDSolver::SetOutLev(const char *name, int value) {
  if (value != 0 && value != 1)
    ReportError("Invalid value {} for option {}") << value << name;
  else
    output_ = value != 0;
}

SSDSolver::SSDSolver()
: Solver<SSDSolver>("ssdsolver", 0, SSDSOLVER_VERSION),
  output_(false), solver_name_("cplex") {
  set_version("SSD Solver");
  AddIntOption("outlev", "0 or 1 (default 0):  Whether to print solution log.",
      &SSDSolver::GetOutLev, &SSDSolver::SetOutLev);
  AddStrOption("solver", "Solver to use for subproblems (default = cplex).",
      &SSDSolver::GetSolverName, &SSDSolver::SetSolverName);
}

void SSDSolver::Solve(Problem &p) {
  Function ssd_uniform;
  int num_scenarios = p.num_logical_cons();
  int num_vars = p.num_vars();
  SSDExtractor extractor(num_scenarios, num_vars);
  for (int i = 0; i < num_scenarios; ++i) {
    LogicalExpr logical_expr = p.logical_con_expr(i);
    RelationalExpr rel_expr = Cast<RelationalExpr>(logical_expr);
    if (!rel_expr || rel_expr.opcode() != NE ||
        Cast<NumericConstant>(rel_expr.rhs()).value() != 0) {
      throw UnsupportedExprError::CreateFromExprString(logical_expr.opname());
    }
    CallExpr call = Cast<CallExpr>(rel_expr.lhs());
    if (!call)
      throw UnsupportedExprError::CreateFromExprString(rel_expr.lhs().opname());
    Function f = call.function();
    if (f == ssd_uniform)
      ; // Do nothing.
    else if (!ssd_uniform && std::strcmp(f.name(), "ssd_uniform") == 0)
      ssd_uniform = f;
    else
      throw UnsupportedExprError::CreateFromExprString(f.name());
    extractor.Extract(call);
  }

  if (p.num_objs() != 0)
    throw Error("SSD solver doesn't support user-defined objectives");

  ProblemChanges pc(p);
  int dominance_var = pc.AddVar(-Infinity, Infinity);
  double coef = 1;
  pc.AddObj(MAX, 1, &coef, &dominance_var);

  // Compute the tails of the reference distribution.
  std::vector<double> ref_tails(extractor.rhs());
  std::sort(ref_tails.begin(), ref_tails.end());
  for (int i = 1; i < num_scenarios; ++i)
    ref_tails[i] += ref_tails[i - 1];

  // Get initial feasible solution.
  Solution sol;
  char solver_msg[] = "solver_msg=0";
  putenv(solver_msg);
  p.Solve(solver_name_, sol, 0, Problem::IGNORE_FUNCTIONS);

  double abs_tolerance = 1e-5;

  // Solve the problem using a cutting-plane method.
  double dominance_lb = -Infinity;
  double dominance_ub =  Infinity;
  std::vector<double> cut_coefs(num_vars + 1);
  const double *coefs = extractor.coefs();
  std::vector<ValueScenario> tails(num_scenarios);
  int iteration = 1;
  printf("\nItn          Gap\n") ;
  for (; sol.status() == Solution::SOLVED; ++iteration) {
    // Compute the tails of the distribution.
    for (int i = 0; i < num_scenarios; ++i) {
      double value = 0;
      const double *row = coefs + i * num_vars;
      for (int j = 0; j < num_vars; ++j)
        value += row[j] * sol.value(j);
      tails[i].value = value;
      tails[i].scenario = i;
    }
    std::sort(tails.begin(), tails.end(), ValueLess());
    for (int i = 1; i < num_scenarios; ++i)
      tails[i].value += tails[i - 1].value;

    // Compute violation and minimal tail difference.
    double min_tail_diff = Infinity;
    double max_rel_violation = 0;
    //int min_tail_diff_scen = -1;
    int max_rel_violation_scen = -1;
    for (int i = 0; i < num_scenarios; ++i) {
      double scaling = 1; // TODO: optional scaling
      double scaled_dominance = dominance_ub * scaling;
      double rel_violation =
          (scaled_dominance + ref_tails[i] + i + 1) / (tails[i].value + i + 1);
      if (rel_violation > max_rel_violation) {
        max_rel_violation = rel_violation;
        max_rel_violation_scen = i;
      }
      double tail_diff = (tails[i].value - ref_tails[i]) / scaling;
      if (tail_diff < min_tail_diff) {
        min_tail_diff = tail_diff;
        //min_tail_diff_scen = i;
      }
    }

    double scaling = 1; // TODO: optional scaling

    // Update the lower bound for the objective which by definition is a
    // minimum of tail differences (possibly scaled).
    if (min_tail_diff > dominance_lb)
      dominance_lb = min_tail_diff;

    fmt::Print("{:3} {:>12}\n") << iteration << (dominance_ub - dominance_lb);

    if ((dominance_ub - dominance_lb) * scaling <= abs_tolerance) {
      fmt::Print("Absolute tolerance reached.\n");
      break;
    }

    // Add a cut.
    for (int i = 0; i < num_vars; ++i) {
      double coef = 0;
      for (int j = 0; j <= max_rel_violation_scen; ++j)
        coef += coefs[tails[j].scenario * num_vars + i];
      cut_coefs[i] = coef;
    }
    cut_coefs[dominance_var] = -scaling;
    pc.AddCon(&cut_coefs[0], ref_tails[max_rel_violation_scen], Infinity);

    p.Solve(solver_name_, sol, &pc, Problem::IGNORE_FUNCTIONS);
    dominance_ub = sol.value(dominance_var);
  }

  // Convert solution status.
  const char *message = 0;
  switch (sol.status()) {
  case Solution::SOLVED:
    message = "optimal solution";
    break;
  case Solution::INFEASIBLE:
    message = "infeasible problem";
    break;
  case Solution::UNBOUNDED:
    message = "unbounded problem";
    break;
  default:
    message = "error";
    break;
  }
  p.set_solve_code(sol.solve_code());

  fmt::Formatter format;
  format("{}: {}") << long_name() << message;
  if (sol.status() == Solution::SOLVED)
    format("; dominance {}") << dominance_ub;
  format("\n{} iteration(s)") << iteration;
  HandleSolution(format.c_str(), sol.values(), 0, 0);
}
}
