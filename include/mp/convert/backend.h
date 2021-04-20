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
#include "mp/convert/converter_query.h"
#include "mp/convert/constraint_keeper.h"
#include "mp/convert/std_constr.h"
#include "mp/convert/std_obj.h"
#include "mp/convert/model.h"
#include "mp/convert/model_adapter.h"

namespace mp {

/// Basic backend wrapper.
/// The basic wrapper provides common functionality: option handling
/// and placeholders for solver API
template <class Impl>
class BasicBackend :
    public BasicConstraintAdder,
    private SolverImpl< ModelAdapter< BasicModel<> > >   // mp::Solver stuff, hidden
{
  ConverterQuery *p_converter_query_object = nullptr;
  using MPSolverBase = SolverImpl< ModelAdapter< BasicModel<> > >;
public:
  using MPUtils = MPSolverBase;              // Allow Converter access the SolverImpl
  const MPUtils& GetMPUtils() const { return *this; }
  MPUtils& GetMPUtils() { return *this; }
public:
  BasicBackend() :
    MPSolverBase(
      Impl::GetSolverInvocationName(),
      Impl::GetAMPLSolverLongName(),
      Impl::Date(), Impl::Flags())
  { }
  virtual ~BasicBackend() { }

  void OpenSolver() { }
  void CloseSolver() { }

  /// Converter should provide this before Backend can run solving
  void ProvideConverterQueryObject(ConverterQuery* pCQ) { p_converter_query_object = pCQ; }

private: // hiding this detail, it's not for the final backends
  const ConverterQuery& GetCQ() const {
    assert(nullptr!=p_converter_query_object);
    return *p_converter_query_object;
  }
  ConverterQuery& GetCQ() {
    assert(nullptr!=p_converter_query_object);
    return *p_converter_query_object;
  }

public:
  void InitOptions() { }

  /// Default metadata
  static const char* GetSolverName() { return "SomeSolver"; }
  static std::string GetSolverVersion() { return "-285.68.53"; }
  static const char* GetSolverInvocationName() { return "solverdirect"; }
  static const char* GetAMPLSolverLongName() { return nullptr; }
  static const char* GetBackendName()    { return "BasicBackend"; }
  static const char* GetBackendLongName() { return nullptr; }
  static long Date() { return MP_DATE; }

  /// Default flags
  static int Flags() {
    int flg=0;
    if (Impl::IfMultipleSol() )
      flg |= Solver::MULTIPLE_SOL;
    if (Impl::IfMultipleObj() )
      flg |= Solver::MULTIPLE_OBJ;
    return flg;
  }
  static bool IfMultipleSol() { return false; }
  static bool IfMultipleObj() { return false; }

  void InitMetaInfoAndOptions() {
    MP_DISPATCH( InitNamesAndVersion() );
    MP_DISPATCH( InitOptions() );
  }

  void InitNamesAndVersion() {
    auto name = MP_DISPATCH( GetSolverName() );
    auto version = MP_DISPATCH( GetSolverVersion() );
    this->set_long_name( fmt::format("{} {}", name, version ) );
    this->set_version( fmt::format("AMPL/{} Optimizer [{}]",
                                   name, version ) );
  }


  using Model = BasicModel<>;

  using Variable = typename Model::Variable;

  void InitProblemModificationPhase() { }
  void FinishProblemModificationPhase() { }
  void AddVariable(Variable var) {
    throw MakeUnsupportedError("BasicBackend::AddVariable");
  }
  void AddCommonExpression(Problem::CommonExpr cexpr) {
    throw MakeUnsupportedError("BasicBackend::AddCommonExpressions");
  }
  void AddLogicalConstraint(Problem::LogicalCon lcon) {
    throw MakeUnsupportedError("BasicBackend::AddLogicalConstraints");
  }

  void AddObjective(typename Model::Objective obj) {
    if (obj.nonlinear_expr()) {
      MP_DISPATCH( AddGeneralObjective( obj ) );
    } else {
      LinearExprUnzipper leu(obj.linear_expr());
      LinearObjective lo { obj.type(),
            std::move(leu.c_), std::move(leu.v_) };
      if (nullptr==obj.p_extra_info()) {
        MP_DISPATCH( AddLinearObjective( lo ) );
      } else {
        auto qt = obj.p_extra_info()->qt_;
        assert(!qt.empty());
        MP_DISPATCH( AddQuadraticObjective(
                       QuadraticObjective{std::move(lo), std::move(qt)} ) );
      }
    }
  }
  void AddGeneralObjective(typename Model::Objective ) {
    throw MakeUnsupportedError("BasicBackend::AddGeneralObjective");
  }
  void AddLinearObjective( const LinearObjective& ) {
    throw MakeUnsupportedError("BasicBackend::AddLinearObjective");
  }
  void AddQuadraticObjective( const QuadraticObjective& ) {
    throw MakeUnsupportedError("BasicBackend::AddQuadraticObjective");
  }

  void AddAlgebraicConstraint(typename Model::AlgebraicCon con) {
    if (con.nonlinear_expr()) {
      MP_DISPATCH( AddGeneralConstraint( con ) );
    } else {
      LinearExprUnzipper leu(con.linear_expr());
      auto lc = LinearConstraint{
          std::move(leu.c_), std::move(leu.v_),
          con.lb(), con.ub() };
      if (nullptr==con.p_extra_info()) {
        MP_DISPATCH( AddConstraint( lc ) );
      } else {
        auto qt = con.p_extra_info()->qt_;
        assert(!qt.empty());
        MP_DISPATCH( AddConstraint( QuadraticConstraint{std::move(lc), std::move(qt)} ) );
      }
    }
  }

  void AddGeneralConstraint(typename Model::AlgebraicCon ) {
    throw MakeUnsupportedError("BasicBackend::AddGeneralConstraint");
  }

  ////////////////// Some basic custom constraints /////////////////
  USE_BASE_CONSTRAINT_HANDLERS(BasicConstraintAdder)

  /// Optionally exclude LDCs from being posted,
  /// then all those are converted to LinearConstraint's first
  ACCEPT_CONSTRAINT(LinearDefiningConstraint, NotAccepted)
  void AddConstraint(const LinearDefiningConstraint& ldc) {
    MP_DISPATCH( AddConstraint(ldc.to_linear_constraint()) );
  }

  ACCEPT_CONSTRAINT(LinearConstraint, Recommended)
  /// TODO Attributes (lazy/user cut, etc)
  void AddConstraint(const LinearConstraint& ) {
    throw MakeUnsupportedError("BasicBackend::AddLinearConstraint");
  }


  void SolveAndReport() {
    MP_DISPATCH( PrepareSolve() );
    MP_DISPATCH( DoSolve() );
    MP_DISPATCH( WrapupSolve() );

    ObtainSolutionStatus();
    ObtainAndReportSolution();
    if (MP_DISPATCH( timing() ))
      PrintTimingInfo();
  }

  void PrepareSolve() {
    MP_DISPATCH( SetInterrupter(MP_DISPATCH( interrupter() )) );
    stats.setup_time = GetTimeAndReset(stats.time);
  }

  void WrapupSolve() {
    stats.solution_time = GetTimeAndReset(stats.time);
  }

  void ObtainSolutionStatus() {
    solve_status = MP_DISPATCH(
          ConvertSolutionStatus(*MP_DISPATCH( interrupter() ), solve_code) );
  }

  void ObtainAndReportSolution() {
    fmt::MemoryWriter writer;
    writer.write("{}: {}", MP_DISPATCH( long_name() ), solve_status);
    if (solve_code < sol::INFEASIBLE) {
      MP_DISPATCH( PrimalSolution(solution) );

      if (MP_DISPATCH( NumberOfObjectives() ) > 0) {
        writer.write("; objective {}",
                     MP_DISPATCH( FormatObjValue(MP_DISPATCH( ObjectiveValue() )) ));
      }
    }
    writer.write("\n");

    if (MP_DISPATCH( IsMIP() )) {
    } else {                                    // Also for QCP
      MP_DISPATCH( DualSolution(dual_solution) );
    }

    HandleSolution(solve_code, writer.c_str(),
                   solution.empty() ? 0 : solution.data(),
                   dual_solution.empty() ? 0 : dual_solution.data(), obj_value);
  }

  void PrintTimingInfo() {
    double output_time = GetTimeAndReset(stats.time);
    MP_DISPATCH( Print("Setup time = {:.6f}s\n"
                       "Solution time = {:.6f}s\n"
                       "Output time = {:.6f}s\n",
                       stats.setup_time, stats.solution_time, output_time) );
  }

  /////////////////////////////// SERVICE STUFF ///////////////////////////////////
  ///
  /////////////////////////////////////////////////////////////////////////////////

  int solve_code=0;
  std::string solve_status;
  double obj_value = std::numeric_limits<double>::quiet_NaN();
  std::vector<double> solution, dual_solution;

  struct Stats {
    steady_clock::time_point time;
    double setup_time;
    double solution_time;
  };
  Stats stats;


  static bool float_equal(double a, double b) {           // ??????
    return std::fabs(a-b) < 1e-8*std::max(std::fabs(a), std::fabs(b));
  }

  bool IsFinite(double n) const {
    return n>MP_DISPATCH( MinusInfinity() ) &&
        n<MP_DISPATCH( Infinity() );
  }
  static double Infinity() { return std::numeric_limits<double>::infinity(); }
  static double MinusInfinity() { return -Infinity(); }

public:

  using Solver::add_to_long_name;
  using Solver::add_to_version;
  using Solver::set_option_header;
  using Solver::add_to_option_header;

protected:
  void HandleSolution(int status, fmt::CStringRef msg,
      const double *x, const double *y, double obj) {
    GetCQ().HandleSolution(status, msg, x, y, obj);
  }

  ///////////////////////////// OPTIONS /////////////////////////////////
  /// TODOs
  /// - hide all Solver stuff behind an abstract interface
protected:

  using Solver::AddOption;

  template <class Value>
  class StoredOption : public mp::TypedSolverOption<Value> {
    Value& value_;
  public:
    using value_type = Value;
    StoredOption(const char *name, const char *description,
                 Value& v, ValueArrayRef values = ValueArrayRef())
      : mp::TypedSolverOption<Value>(name, description, values), value_(v) {}

    void GetValue(Value &v) const override { v = value_; }
    void SetValue(typename internal::OptionHelper<Value>::Arg v) override
    { value_ = v; }
  };

  /// Solver options accessor, facilitates calling
  /// backend_.Get/SetSolverOption()
  template <class Value, class Index>
  class SolverOptionAccessor {
    using Backend = Impl;
    Backend& backend_;
  public:
    using value_type = Value;
    using index_type = Index;
    SolverOptionAccessor(Backend& b) : backend_(b) { }
    /// Options setup
    Value get(const SolverOption& , Index i) const {
      Value v;
      backend_.GetSolverOption(i, v);
      return v;
    }
    void set(const SolverOption& ,
             typename internal::OptionHelper<Value>::Arg v,
             Index i) {
      backend_.SetSolverOption(i, v); }
  };

  template <class ValueType, class KeyType>
  class ConcreteOptionWrapper :
      public Solver::ConcreteOptionWithInfo<
      SolverOptionAccessor<ValueType, KeyType>, ValueType, KeyType> {

    using COType = Solver::ConcreteOptionWithInfo<
    SolverOptionAccessor<ValueType, KeyType>, ValueType, KeyType>;
    using SOAType = SolverOptionAccessor<ValueType, KeyType>;

    SOAType soa_;
  public:
    ConcreteOptionWrapper(Impl* impl_, const char *name, const char *description,
                          KeyType k) :
      COType(name, description, &soa_, &SOAType::get, &SOAType::set, k),
      soa_(*impl_)
    { }
  };

public:

  /// Simple stored option referencing a variable
  template <class Value>
  void AddStoredOption(const char *name, const char *description,
                       Value& value, ValueArrayRef values = ValueArrayRef()) {
    AddOption(Solver::OptionPtr(
                new StoredOption<Value>(
                  name, description, value, values)));
  }

  /// Adding solver options of types int/double/string/...
  /// The type is deduced from the two last parameters min, max
  /// (currently unused otherwise - TODO)
  /// If min/max omitted, assume ValueType=std::string
  /// Assumes existence of Impl::Get/SetSolverOption(KeyType, ValueType(&))
  template <class KeyType, class ValueType=std::string>
  void AddSolverOption(const char *name, const char *description,
                       KeyType k,
                       ValueType ={}, ValueType ={}) {
    AddOption(Solver::OptionPtr(
                new ConcreteOptionWrapper<
                ValueType, KeyType>(
                  (Impl*)this, name, description, k)));
  }
  /// TODO use vmin/vmax or rely on solver raising error?
  /// TODO also with ValueTable, deduce type from it

};


}  // namespace mp

#endif  // BACKEND_H_