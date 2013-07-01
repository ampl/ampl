/*
 AMPL solver interface to JaCoP.

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

#include "solvers/jacop/jacop.h"

#include <iterator>

namespace {

// TODO
/*const ampl::OptionValue<Gecode::IntVarBranch> VAR_BRANCHINGS[] = {
    {"none",            Gecode::INT_VAR_NONE},
    {"rnd",             Gecode::INT_VAR_RND},
    {"degree_min",      Gecode::INT_VAR_DEGREE_MIN},
    {"degree_max",      Gecode::INT_VAR_DEGREE_MAX},
    {"afc_min",         Gecode::INT_VAR_AFC_MIN},
    {"afc_max",         Gecode::INT_VAR_AFC_MAX},
    {"min_min",         Gecode::INT_VAR_MIN_MIN},
    {"min_max",         Gecode::INT_VAR_MIN_MAX},
    {"max_min",         Gecode::INT_VAR_MAX_MIN},
    {"max_max",         Gecode::INT_VAR_MAX_MAX},
    {"size_min",        Gecode::INT_VAR_SIZE_MIN},
    {"size_max",        Gecode::INT_VAR_SIZE_MAX},
    {"size_degree_min", Gecode::INT_VAR_SIZE_DEGREE_MIN},
    {"size_degree_max", Gecode::INT_VAR_SIZE_DEGREE_MAX},
    {"size_afc_min",    Gecode::INT_VAR_SIZE_AFC_MIN},
    {"size_afc_max",    Gecode::INT_VAR_SIZE_AFC_MAX},
    {"regret_min_min",  Gecode::INT_VAR_REGRET_MIN_MIN},
    {"regret_min_max",  Gecode::INT_VAR_REGRET_MIN_MAX},
    {"regret_max_min",  Gecode::INT_VAR_REGRET_MAX_MIN},
    {"regret_max_max",  Gecode::INT_VAR_REGRET_MAX_MAX},
    {}
};

const ampl::OptionValue<Gecode::IntValBranch> VAL_BRANCHINGS[] = {
    {"min",        Gecode::INT_VAL_MIN},
    {"med",        Gecode::INT_VAL_MED},
    {"max",        Gecode::INT_VAL_MAX},
    {"rnd",        Gecode::INT_VAL_RND},
    {"split_min",  Gecode::INT_VAL_SPLIT_MIN},
    {"split_max",  Gecode::INT_VAL_SPLIT_MAX},
    {"range_min",  Gecode::INT_VAL_RANGE_MIN},
    {"range_max",  Gecode::INT_VAL_RANGE_MAX},
    {"values_min", Gecode::INT_VALUES_MIN},
    {"values_max", Gecode::INT_VALUES_MAX},
    {}
};*/
}

namespace ampl {

jint NLToJaCoPConverter::CastToInt(double value) const {
  jint int_value = static_cast<jint>(value);
  if (int_value != value) {
    throw UnsupportedExprError::CreateFromMessage(
        fmt::Format("value {} can't be represented as int") << value);
  }
  if (int_value < min_int_ || int_value > max_int_)
    throw Error(str(fmt::Format("value {} is out of bounds") << value));
  return int_value;
}

jobject NLToJaCoPConverter::Convert(
    IteratedLogicalExpr e, ClassBase &cls, jmethodID &ctor) {
  if (!ctor) {
    cls.Init(env_);
    ctor = env_.GetMethod(cls.get(),
        "<init>", "([LJaCoP/constraints/PrimitiveConstraint;)V");
  }
  if (!constraint_class_) {
    constraint_class_ = env_.FindClass(
        "JaCoP/constraints/PrimitiveConstraint");
  }
  int num_args = e.num_args();
  jobjectArray args = env_.NewObjectArray(num_args, constraint_class_, 0);
  for (int i = 0; i < num_args; ++i)
    env_.SetObjectArrayElement(args, i, Visit(e[i]));
  return env_.NewObject(cls.get(), ctor, args);
}

void NLToJaCoPConverter::RequireZeroRHS(
    BinaryExpr e, const std::string &func_name) {
  NumericConstant num = Cast<NumericConstant>(e.rhs());
  if (!num || num.value() != 0) {
    throw UnsupportedExprError::CreateFromExprString(
        func_name + " with nonzero second parameter");
  }
}

template <typename Term>
void NLToJaCoPConverter::ConvertExpr(
    LinearExpr<Term> linear, NumericExpr nonlinear, jobject result_var) {
  jsize num_terms = std::distance(linear.begin(), linear.end());
  if (nonlinear) {
    NumericConstant n = Cast<NumericConstant>(nonlinear);
    if (n && n.value() == 0)
      nonlinear = NumericExpr();
  }
  if (num_terms != 0) {
    if (nonlinear)
      ++num_terms;
    std::vector<jint> coefs(num_terms);
    jobjectArray vars = CreateVarArray(num_terms);
    int index = 0;
    for (typename LinearExpr<Term>::iterator
        i = linear.begin(), end = linear.end(); i != end; ++i, ++index) {
      coefs[index] = CastToInt(i->coef());
      env_.SetObjectArrayElement(vars, index, vars_[i->var_index()]);
    }
    if (nonlinear) {
      assert(index == num_terms - 1);
      coefs[index] = 1;
      env_.SetObjectArrayElement(vars, index, Visit(nonlinear));
    }
    jintArray coefs_array = env_.NewIntArray(num_terms);
    env_.SetIntArrayRegion(coefs_array, 0, num_terms, &coefs[0]);
    Impose(sum_weight_class_.NewObject(env_, vars, coefs_array, result_var));
  } else if (nonlinear)
    Impose(eq_class_.NewObject(env_, Visit(nonlinear), result_var));
}

void NLToJaCoPConverter::ConvertLogicalCon(LogicalExpr e, bool post) {
  AllDiffExpr alldiff = Cast<AllDiffExpr>(e);
  if (!alldiff) {
    Impose(Visit(e));
    return;
  }
  int num_args = alldiff.num_args();
  jobjectArray args = CreateVarArray(num_args);
  for (int i = 0; i < num_args; ++i) {
    NumericExpr arg = alldiff[i];
    jobject result_var = 0;
    if (Variable var = Cast<Variable>(arg))
      result_var = vars_[var.index()];
    else
      result_var = Visit(arg);
    env_.SetObjectArrayElement(args, i, result_var);
  }
  Impose(alldiff_class_.NewObject(env_, args));
}

NLToJaCoPConverter::NLToJaCoPConverter()
: env_(JVM::env()), store_(), impose_(), var_array_(),
  constraint_class_(), or_array_ctor_(), and_array_ctor_(), one_var_() {
  var_class_.Init(env_);
  jclass domain_class = env_.FindClass("JaCoP/core/IntDomain");
  min_int_ = env_.GetStaticIntField(
      domain_class, env_.GetStaticFieldID(domain_class, "MinInt", "I"));
  max_int_ = env_.GetStaticIntField(
      domain_class, env_.GetStaticFieldID(domain_class, "MaxInt", "I"));
}

jobject NLToJaCoPConverter::VisitPlus(BinaryExpr e) {
  NumericExpr lhs = e.lhs(), rhs = e.rhs();
  if (NumericConstant c = Cast<NumericConstant>(lhs))
    return CreateCon(plus_const_class_, Visit(e.rhs()), CastToInt(c.value()));
  if (NumericConstant c = Cast<NumericConstant>(rhs))
    return CreateCon(plus_const_class_, Visit(e.lhs()), CastToInt(c.value()));
  return CreateCon(plus_class_, Visit(lhs), Visit(rhs));
}

jobject NLToJaCoPConverter::VisitNumericLess(BinaryExpr e) {
  jobjectArray args = CreateVarArray(2);
  env_.SetObjectArrayElement(args, 0,
      CreateMinus(Visit(e.lhs()), Visit(e.rhs())));
  env_.SetObjectArrayElement(args, 1, CreateConst(0));
  return CreateCon(max_class_, args);
}

void NLToJaCoPConverter::Convert(const Problem &p) {
  if (p.num_continuous_vars() != 0)
    throw Error("JaCoP doesn't support continuous variables");

  jclass store_class = env_.FindClass("JaCoP/core/Store");
  store_ = env_.NewObject(store_class,
      env_.GetMethod(store_class, "<init>", "()V"));
  impose_ = env_.GetMethod(store_class,
      "impose", "(LJaCoP/constraints/Constraint;)V");

  int num_vars = p.num_integer_vars();
  var_array_ = CreateVarArray(num_vars);
  vars_.resize(num_vars);
  for (int j = 0; j < num_vars; ++j) {
    double lb = p.var_lb(j), ub = p.var_ub(j);
    jobject var = var_class_.NewObject(env_, store_,
        lb <= negInfinity ? min_int_ : CastToInt(lb),
        ub >= Infinity ? max_int_ : CastToInt(ub));
    vars_[j] = var;
    env_.SetObjectArrayElement(var_array_, j, var);
  }

  if (p.num_objs() != 0) {
    jobject result_var = var_class_.NewObject(env_, store_, min_int_, max_int_);
    ConvertExpr(p.linear_obj_expr(0), p.nonlinear_obj_expr(0), result_var);
    obj_ = p.obj_type(0) == MIN ?
        result_var : CreateCon(mul_const_class_, result_var, -1);
  }

  // Convert constraints.
  for (int i = 0, n = p.num_cons(); i < n; ++i) {
    double lb = p.con_lb(i), ub = p.con_ub(i);
    jint int_lb = lb <= negInfinity ? min_int_ : CastToInt(lb);
    jint int_ub = ub >= Infinity ? max_int_ : CastToInt(ub);
    jobject result_var = var_class_.NewObject(env_, store_, int_lb, int_ub);
    ConvertExpr(p.linear_con_expr(i), p.nonlinear_con_expr(i), result_var);
  }

  // Convert logical constraints.
  for (int i = 0, n = p.num_logical_cons(); i < n; ++i)
    ConvertLogicalCon(p.logical_con_expr(i));
}

jobject NLToJaCoPConverter::VisitIf(IfExpr e) {
  jobject result_var = CreateVar();
  Impose(if_else_class_.NewObject(env_, Visit(e.condition()),
      eq_class_.NewObject(env_, result_var, Visit(e.true_expr())),
      eq_class_.NewObject(env_, result_var, Visit(e.false_expr()))));
  return result_var;
}

jobject NLToJaCoPConverter::VisitSum(SumExpr e) {
  jobjectArray args = CreateVarArray(e.num_args());
  int index = 0;
  for (SumExpr::iterator i = e.begin(), end = e.end(); i != end; ++i)
    env_.SetObjectArrayElement(args, index++, Visit(*i));
  return CreateCon(sum_class_, args);
}

jobject NLToJaCoPConverter::VisitCount(CountExpr e) {
  jobjectArray args = CreateVarArray(e.num_args());
  int index = 0;
  for (CountExpr::iterator i = e.begin(), end = e.end(); i != end; ++i) {
    jobject result_var = CreateVar();
    Impose(if_else_class_.NewObject(env_, Visit(*i),
        eq_const_class_.NewObject(env_, result_var, 1),
        eq_const_class_.NewObject(env_, result_var, 0)));
    env_.SetObjectArrayElement(args, index++, result_var);
  }
  return CreateCon(sum_class_, args);
}

jobject NLToJaCoPConverter::VisitNumberOf(NumberOfExpr e) {
  // JaCoP only supports count constraints with constant value.
  NumericConstant num = Cast<NumericConstant>(e.value());
  if (!num) {
    throw UnsupportedExprError::CreateFromExprString(
        "numberof with variable value");
  }
  jobject result_var = CreateVar();
  int num_args = e.num_args();
  jobjectArray args = CreateVarArray(num_args);
  for (int i = 0; i < num_args; ++i)
    env_.SetObjectArrayElement(args, i, Visit(e[i]));
  Impose(count_class_.NewObject(
      env_, args, result_var, CastToInt(num.value())));
  return result_var;
}

jobject NLToJaCoPConverter::VisitImplication(ImplicationExpr e) {
  jobject condition = Visit(e.condition());
  return if_else_class_.NewObject(env_, condition,
      Visit(e.true_expr()), Visit(e.false_expr()));
}

/*JaCoPSolver::Stop::Stop(JaCoPSolver &s)
: sh_(s), solver_(s), time_limit_in_milliseconds_(s.time_limit_ * 1000),
  last_output_time_(0) {
  output_or_limit_ = s.output_ || time_limit_in_milliseconds_ < DBL_MAX ||
      s.node_limit_ != ULONG_MAX || s.fail_limit_ != ULONG_MAX ||
      s.memory_limit_ != std::numeric_limits<std::size_t>::max();
  timer_.start();
}

bool JaCoPSolver::Stop::stop(
    const Search::Statistics &s, const Search::Options &) {
  if (SignalHandler::stop()) return true;
  if (!output_or_limit_) return false;
  double time = timer_.stop();
  if (solver_.output_ &&
      (time - last_output_time_) / 1000 >= solver_.output_frequency_) {
    solver_.Output("{:10} {:10} {:10}\n") << s.depth << s.node << s.fail;
    last_output_time_ = time;
  }
  return time > time_limit_in_milliseconds_ || s.node > solver_.node_limit_ ||
      s.fail > solver_.fail_limit_ || s.memory > solver_.memory_limit_;
}

void JaCoPSolver::SetBoolOption(const char *name, int value, bool *option) {
  if (value != 0 && value != 1)
    ReportInvalidOptionValue(name, value);
  else
    *option = value != 0;
}

// TODO
void JaCoPSolver::SetOutputFrequency(const char *name, int value) {
  if (value <= 0)
    ReportError("Invalid value {} for option {}") << value << name;
  else
    output_frequency_ = value;
}

template <typename T>
void JaCoPSolver::SetStrOption(const char *name, const char *value,
    const OptionInfo<T> &info) {
  for (const OptionValue<T> *p = info.values; p->name; ++p) {
    if (std::strcmp(value, p->name) == 0) {
      info.value = p->value;
      return;
    }
  }
  ReportError("Invalid value {} for option {}") << value << name;
}

template <typename T, typename OptionT>
void JaCoPSolver::SetOption(const char *name, T value, OptionT *option) {
  if (value < 0)
    ReportError("Invalid value {} for option {}") << value << name;
  else
    *option = value;
}

fmt::TempFormatter<fmt::Write> JaCoPSolver::Output(fmt::StringRef format) {
  if (output_count_ == 0)
    fmt::Print("{}") << header_;
  output_count_ = (output_count_ + 1) % 20;
  return fmt::TempFormatter<fmt::Write>(format);
}*/

void JaCoPSolver::HandleUnknownOption(const char *name) {
  if (name[0] == '-') {
    fmt::Print("{}\n") << name;
    jvm_options_.push_back(name);
  } else {
    BasicSolver::HandleUnknownOption(name);
  }
}

JaCoPSolver::JaCoPSolver()
: Solver<JaCoPSolver>("jacop", 0, 20130701) {

  // TODO: options
  /*output_(false), output_frequency_(1), output_count_(0),
  var_branching_(Gecode::INT_VAR_SIZE_MIN),
  val_branching_(Gecode::INT_VAL_MIN),
  time_limit_(DBL_MAX), node_limit_(ULONG_MAX), fail_limit_(ULONG_MAX),
  memory_limit_(std::numeric_limits<std::size_t>::max()) {

  set_version("JaCoP " GECODE_VERSION);

  // TODO
  /*AddIntOption("outlev",
      "0 or 1 (default 0):  Whether to print solution log.",
      &JaCoPSolver::EnableOutput);

  AddIntOption("outfreq",
      "Output frequency in seconds.  The value should be a positive integer.",
      &JaCoPSolver::SetOutputFrequency);

  AddStrOption("var_branching",
      "Variable branching.  Possible values:\n"
      "      none            - first unassigned\n"
      "      rnd             - random\n"
      "      degree_min      - smallest degree\n"
      "      degree_max      - largest degree\n"
      "      afc_min         - smallest accumulated failure count (AFC)\n"
      "      afc_max         - largest accumulated failure count (AFC)\n"
      "      min_min         - smallest minimum value\n"
      "      min_max         - largest minimum value\n"
      "      max_min         - smallest maximum value\n"
      "      max_max         - largest maximum value\n"
      "      size_min        - smallest domain size (default)\n"
      "      size_max        - largest domain size\n"
      "      size_degree_min - smallest domain size divided by degree\n"
      "      size_degree_max - largest domain size divided by degree\n"
      "      size_afc_min    - smallest domain size divided by AFC\n"
      "      size_afc_max    - largest domain size divided by AFC\n"
      "      regret_min_min  - smallest minimum-regret\n"
      "      regret_min_max  - largest minimum-regret\n"
      "      regret_max_min  - smallest maximum-regret\n"
      "      regret_max_max  - largest maximum-regret\n",
      &JaCoPSolver::SetStrOption<Gecode::IntVarBranch>,
      OptionInfo<Gecode::IntVarBranch>(VAR_BRANCHINGS, var_branching_));

  AddStrOption("val_branching",
      "Value branching.  Possible values:\n"
      "      min        - smallest value (default)\n"
      "      med        - greatest value not greater than the median\n"
      "      max        - largest value\n"
      "      rnd        - random value\n"
      "      split_min  - values not greater than mean of smallest and\n"
      "                   largest value\n"
      "      split_max  - values greater than mean of smallest and largest\n"
      "                   value\n"
      "      range_min  - values from smallest range, if domain has several\n"
      "                   ranges; otherwise, values not greater than mean of\n"
      "                   smallest and largest value\n"
      "      range_max  - values from largest range, if domain has several\n"
      "                   ranges; otherwise, values greater than mean of\n"
      "                   smallest and largest value\n"
      "      values_min - all values starting from smallest\n"
      "      values_min - all values starting from largest\n",
      &JaCoPSolver::SetStrOption<Gecode::IntValBranch>,
      OptionInfo<Gecode::IntValBranch>(VAL_BRANCHINGS, val_branching_));

  AddDblOption("threads",
      "The number of parallel threads to use.  Assume that your computer\n"
      "has m processing units and that the value for threads is n.\n"
      "    * If n = 0, then m threads are used (as many as available\n"
      "      processing units).\n"
      "    * If n >= 1, then n threads are used (absolute number of\n"
      "      threads to be used).\n"
      "    * If n <= −1, then m + n threads are used (absolute number\n"
      "      of processing units not to be used). For example, when\n"
      "      n = −6 and m = 8, then 2 threads are used.\n"
      "    * If 0 < n < 1, then n * m threads are used (relative number\n"
      "      of processing units to be used). For example, when n = 0.5\n"
      "      and m = 8, then 4 threads are used.\n"
      "    * If −1 < n < 0, then (1 + n) * m threads are used (relative\n"
      "      number of processing units not to be used). For example,\n"
      "      when n = −0.25 and m = 8, then 6 threads are used.\n"
      "All values are rounded and at least one thread is used.\n",
      &JaCoPSolver::SetDblOption, &options_.threads);

  AddIntOption("c_d", "Commit recomputation distance.",
      &JaCoPSolver::SetOption<int, unsigned>, &options_.c_d);
  AddIntOption("a_d", "Adaptive recomputation distance.",
      &JaCoPSolver::SetOption<int, unsigned>, &options_.a_d);

  AddDblOption("timelimit", "Time limit.",
      &JaCoPSolver::SetOption<double, double>, &time_limit_);
  AddIntOption("nodelimit", "Node limit.",
      &JaCoPSolver::SetOption<int, unsigned long>, &node_limit_);
  AddIntOption("faillimit", "Fail limit.",
      &JaCoPSolver::SetOption<int, unsigned long>, &fail_limit_);
  AddIntOption("memorylimit", "Memory limit.",
      &JaCoPSolver::SetOption<int, std::size_t>, &memory_limit_);*/
}

std::string JaCoPSolver::GetOptionHeader() {
  return
      "JaCoP Directives for AMPL\n"
      "--------------------------\n"
      "\n"
      "To set these directives, assign a string specifying their values to "
      "the AMPL option jacop_options.  For example:\n"
      "\n";
  // TODO: example
      //"  ampl: option gecode_options 'version nodelimit=30000 val_branching=min';\n";
}

void JaCoPSolver::Solve(Problem &p) {
  std::vector<const char*> jvm_options(jvm_options_.size() + 1);
  for (size_t i = 0, n = jvm_options_.size(); i != n; ++i)
    jvm_options[i] = jvm_options_[i].c_str();
  Env env = JVM::env(&jvm_options[0]);

  // Set up an optimization problem in JaCoP.
  NLToJaCoPConverter converter;
  converter.Convert(p);

  // Solve the problem.
  SignalHandler signal_handler(*this); // TODO
  Class<DepthFirstSearch> dfs_class;
  jobject search = dfs_class.NewObject(env);
  jmethodID setPrintInfo =
      env.GetMethod(dfs_class.get(), "setPrintInfo", "(Z)V");
  env.CallVoidMethod(search, setPrintInfo, false);
  jobject indomain = env.NewObject("JaCoP/search/IndomainMin", "()V");
  jobject select = env.NewObject("JaCoP/search/InputOrderSelect",
      "(LJaCoP/core/Store;[LJaCoP/core/Var;LJaCoP/search/Indomain;)V",
      converter.store(), converter.var_array(), indomain);
  double obj_val = std::numeric_limits<double>::quiet_NaN();
  bool has_obj = p.num_objs() != 0;
  // TODO
  /*bool stopped = false;
  header_ = str(fmt::Format("{:>10} {:>10} {:>10} {:>13}\n")
    << "Max Depth" << "Nodes" << "Fails" << (has_obj ? "Best Obj" : ""));*/
  jboolean found = false;
  if (has_obj) {
    jmethodID labeling = env.GetMethod(dfs_class.get(), "labeling",
        "(LJaCoP/core/Store;LJaCoP/search/SelectChoicePoint;"
        "LJaCoP/core/IntVar;)Z");
    found = env.CallBooleanMethod(
        search, labeling, converter.store(), select, converter.obj());
  } else {
    jmethodID labeling = env.GetMethod(dfs_class.get(), "labeling",
        "(LJaCoP/core/Store;LJaCoP/search/SelectChoicePoint;)Z");
    found = env.CallBooleanMethod(search, labeling, converter.store(), select);
  }

  // Convert solution status.
  const char *status = 0;
  int solve_code = 0;
  // TODO
  /*if (stopped) {
    solve_code = 600;
    status = "interrupted";
  } else*/
  if (!found) {
    solve_code = 200;
    status = "infeasible problem";
  } else if (has_obj) {
    solve_code = 0;
    status = "optimal solution";
  } else {
    solve_code = 100;
    status = "feasible solution";
  }
  p.set_solve_code(solve_code);

  std::vector<double> final_solution;
  if (found) {
    jclass var_class = converter.var_class().get();
    jmethodID value = env.GetMethod(var_class, "value", "()I");
    if (has_obj) {
      obj_val = env.CallIntMethod(converter.obj(), value);
      if (p.obj_type(0) == MAX)
        obj_val = -obj_val;
    }
    const std::vector<jobject> &vars = converter.vars();
    int num_vars = p.num_vars();
    final_solution.resize(num_vars);
    for (int j = 0; j < num_vars; ++j)
      final_solution[j] = env.CallIntMethod(vars[j], value);
  }

  fmt::Formatter format;
  format("{}: {}\n") << long_name() << status;
  jmethodID get_nodes = env.GetMethod(dfs_class.get(), "getNodes", "()I");
  jmethodID get_fails =
      env.GetMethod(dfs_class.get(), "getWrongDecisions", "()I");
  format("{} nodes, {} fails") << env.CallIntMethod(search, get_nodes)
      << env.CallIntMethod(search, get_fails);
  if (has_obj && found)
    format(", objective {}") << ObjPrec(obj_val);
  HandleSolution(format.c_str(),
      final_solution.empty() ? 0 : &final_solution[0], 0, obj_val);
}
}
