/*
 Abstract solver backend wrapper.

 Copyright (C) 2020 AMPL Optimization Inc

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

 Author: Gleb Belov <gleb.belov@monash.edu>
 */

#ifndef BACKEND_H_
#define BACKEND_H_

#include "mp/clock.h"
#include "mp/convert/model.h"
#include "mp/solver.h"
#include "mp/convert/constraint_keeper.h"
#include "mp/convert/std_constr.h"

namespace mp {

/// Basic backend wrapper.
/// Used by converter to directly access a solver.
/// The basic wrapper provides common functionality: option handling
/// and placeholders for solver API
template <class Impl, class Model = BasicModel<std::allocator<char>>>
class BasicBackend : public BasicConstraintAdder,
    public SolverImpl<Model>  // TODO no SolverImpl
{
public:
  BasicBackend(fmt::CStringRef name, fmt::CStringRef longname=0,
               long date=0, int flags=0) :
    SolverImpl<Model>(name, longname, date, flags) { }

  void InitProblemModificationPhase(const Problem& p) { }  // TODO Get rid of Problem here
  void FinishProblemModificationPhase() { }
  void AddVariables(int n, double* lbs, double* ubs, mp::var::Type* types) {
    throw MakeUnsupportedError("BasicBackend::AddVariables");
  }
  void AddCommonExpression(Problem::CommonExpr cexpr) {
    throw MakeUnsupportedError("BasicBackend::AddCommonExpressions");
  }
  void AddLogicalConstraint(Problem::LogicalCon lcon) {
    throw MakeUnsupportedError("BasicBackend::AddLogicalConstraints");
  }

  void AddObjective(Problem::Objective obj) {
    if (obj.nonlinear_expr()) {
      MP_DISPATCH( AddGeneralObjective( obj ) );
    } else {
      LinearExprUnzipper leu(obj.linear_expr());
      MP_DISPATCH( AddLinearObjective( obj.type(), leu.c_.size(),
                                       leu.c_.data(), leu.v_.data()) );
    }
    }
  void AddLinearObjective( obj::Type sense, int nnz,
                           const double* c, const int* v) {
    throw MakeUnsupportedError("BasicBackend::AddLinearObjective");
  }
  void AddGeneralObjective(Problem::Objective obj) {
    throw MakeUnsupportedError("BasicBackend::AddGeneralObjective");
  }

  void AddAlgebraicConstraint(Problem::AlgebraicCon con) {
    if (con.nonlinear_expr()) {
      MP_DISPATCH( AddGeneralConstraint( con ) );
    } else {
      LinearExprUnzipper leu(con.linear_expr());
      MP_DISPATCH( AddLinearConstraint(leu.c_.size(), leu.c_.data(), leu.v_.data(),
                                       con.lb(), con.ub()) );
    }
    }

  /// TODO Do we need ability to add several at once?
  /// TODO Attributes (lazy/user cut, etc)
  void AddLinearConstraint(int nnz, const double* c, const int* v,
                           double lb, double ub) {
    throw MakeUnsupportedError("BasicBackend::AddLinearConstraint");
  }
  void AddGeneralConstraint(Problem::AlgebraicCon con) {
    throw MakeUnsupportedError("BasicBackend::AddGeneralConstraint");
  }

  ////////////////// Some basic custom constraints /////////////////
  USE_BASE_CONSTRAINT_HANDLERS(BasicConstraintAdder)

  /// Optionally exclude LFDs from being posted,
  /// then all those are converted to LinearConstraint's first
  ACCEPT_CONSTRAINT(LinearDefiningConstraint, NotAccepted)
  void AddConstraint(const LinearDefiningConstraint& ldc) {
    MP_DISPATCH( AddConstraint(ldc.to_linear_constraint()) );
  }

  ACCEPT_CONSTRAINT(LinearConstraint, Recommended)
  void AddConstraint(const LinearConstraint& ldc) {     // TODO make this form primary
    MP_DISPATCH( AddLinearConstraint(ldc.nnz(), ldc.coefs(), ldc.vars(),
                                     ldc.lb(), ldc.ub()) );
  }


  void Solve(Problem &p, SolutionHandler &sh) { Resolve(p, sh); }

  void Resolve(Problem& p, SolutionHandler &sh) {
    MP_DISPATCH( SetInterrupter(MP_DISPATCH( interrupter() )) );

    MP_DISPATCH( Print( "Exporting model.lp.\n" ) );
    MP_DISPATCH( ExportModel("model.lp") );

    stats.setup_time = GetTimeAndReset(stats.time);
    MP_DISPATCH( DoOptimize() );
    stats.solution_time = GetTimeAndReset(stats.time);

    // Convert solution status.
    int solve_code = 0;
    std::string status = MP_DISPATCH(
        ConvertSolutionStatus(*MP_DISPATCH( interrupter() ), solve_code) );

    fmt::MemoryWriter writer;
    writer.write("{}: {}\n", MP_DISPATCH( long_name() ), status);
    double obj_value = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> solution, dual_solution;
    if (solve_code < sol::INFEASIBLE) {
      MP_DISPATCH( PrimalSolution(solution) );

      if (MP_DISPATCH( IsMIP() )) {
        writer << MP_DISPATCH( NodeCount() ) << " nodes, ";
      } else {                                    // Also for QCP
        MP_DISPATCH( DualSolution(dual_solution) );
      }
      writer << MP_DISPATCH( Niterations() ) << " iterations";

      if (MP_DISPATCH( NumberOfObjectives() ) > 0) {
        writer.write(", objective {}",
                     MP_DISPATCH( FormatObjValue(MP_DISPATCH( ObjectiveValue() )) ));
      }
    }
    sh.HandleSolution(solve_code, writer.c_str(),
        solution.empty() ? 0 : solution.data(),
        dual_solution.empty() ? 0 : dual_solution.data(), obj_value);

    double output_time = GetTimeAndReset(stats.time);

    if (MP_DISPATCH( timing() )) {
      MP_DISPATCH( Print("Setup time = {:.6f}s\n"
            "Solution time = {:.6f}s\n"
            "Output time = {:.6f}s\n",
            stats.setup_time, stats.solution_time, output_time) );
    }
  }



  /////////////////////////////// SERVICE STUFF ///////////////////////////////////
  ///
  /////////////////////////////////////////////////////////////////////////////////

  struct Stats {
    steady_clock::time_point time;
    double setup_time;
    double solution_time;
  };
  Stats stats;


  static bool float_equal(double a, double b) {           // ??????
    return std::fabs(a-b) < 1e-8*std::max(std::fabs(a), std::fabs(b));
  }

};


}  // namespace mp

#endif  // BACKEND_H_