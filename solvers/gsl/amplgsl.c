/* AMPL bindings for the GNU Scientific Library. */

#include <math.h>
#include <stdarg.h>

#include <gsl/gsl_errno.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_complex_math.h>
#include <gsl/gsl_sf_airy.h>
#include <gsl/gsl_sf_bessel.h>
#include <gsl/gsl_sf_clausen.h>
#include <gsl/gsl_sf_coulomb.h>
#include <gsl/gsl_sf_coupling.h>
#include <gsl/gsl_sf_dawson.h>
#include <gsl/gsl_sf_debye.h>
#include <gsl/gsl_sf_dilog.h>
#include <gsl/gsl_sf_ellint.h>

#include "solvers/funcadd.h"

enum { MAX_ERROR_MESSAGE_SIZE = 100 };

/* Computes (x / fabs(x)) * y. Returns 0 if y is 0. */
static double mul_by_sign(double x, double y) {
  return y != 0 ? (x / fabs(x)) * y : 0;
}

/* Formats the error message and stores it in al->Errmsg. */
static void error(arglist *al, const char *format, ...) {
  va_list args;
  al->Errmsg = al->AE->Tempmem(al->TMI, MAX_ERROR_MESSAGE_SIZE);
  va_start(args, format);
  al->AE->VsnprintF(al->Errmsg, MAX_ERROR_MESSAGE_SIZE, format, args);
  va_end(args);
}

/*
 * Checks the arguments of a zero function such as gsl_sf_airy_Ai_scaled:
 * - argument with the specified index should be representable as unsigned int
 * - al->derivs should be null
 */
static int check_zero_func_args(arglist *al, unsigned s_index) {
  real arg = al->ra[s_index];
  if ((unsigned)arg != arg) {
    error(al, "argument 's' can't be represented as unsigned int, s = %g", arg);
    return 0;
  }
  if (al->derivs) {
    /* Derivative information is requested, so the argument is not constant. */
    error(al, "argument 's' is not constant");
    return 0;
  }
  return 1;
}

/* Checks if the argument is within the bounds for derivative computation. */
static int check_deriv_arg(arglist *al, int arg, int min, int max) {
  if (arg < min) {
    error(al, "can't compute derivative: argument 'n' too small, n = %d", arg);
    return 0;
  }
  if (arg > max) {
    error(al, "can't compute derivative: argument 'n' too large, n = %d", arg);
    return 0;
  }
  return 1;
}

/* Reports a function evaluation error printing the specified suffix
 * after the function name. */
static void eval_error_with_suffix(arglist *al,
    const char *func_name, const char *suffix) {
  int n = 0, i = 0;
  al->Errmsg = al->AE->Tempmem(al->TMI, MAX_ERROR_MESSAGE_SIZE);
  n += al->AE->SnprintF(al->Errmsg, MAX_ERROR_MESSAGE_SIZE,
      "can't evaluate %s%s(", func_name, suffix);
  for (i = 0; i < al->n - 1; ++i) {
    n += al->AE->SnprintF(al->Errmsg + n, MAX_ERROR_MESSAGE_SIZE - n,
        "%g, ", al->ra[i]);
  }
  al->AE->SnprintF(al->Errmsg + n, MAX_ERROR_MESSAGE_SIZE - n,
      "%g)", al->ra[al->n - 1]);
}

/* Reports a function evaluation error. */
static void eval_error(arglist *al, const char *func_name) {
  eval_error_with_suffix(al, func_name, "");
}

static double check_result(arglist *al, double result, const char *func_name) {
  int i = 0, n = 0;
  if (gsl_isnan(result)) {
    eval_error(al, func_name);
    return 0;
  }
  if (al->derivs) {
    for (i = 0; i < al->n; ++i) {
      if (gsl_isnan(al->derivs[i])) {
        eval_error_with_suffix(al, func_name, "'");
        return 0;
      }
    }
    if (al->hes) {
      for (i = 0, n = al->n * (al->n + 1) / 2; i < n; ++i) {
        if (gsl_isnan(al->hes[i])) {
          eval_error_with_suffix(al, func_name, "''");
          return 0;
        }
      }
    }
  }
  return result;
}

/* Flags for check_bessel_args */
enum {
  DERIV_INT_MIN = 1 /* Derivative can be computed for n = INT_MIN */
};

/*
 * Checks whether the first argument is constant and reports error if not.
 * Returns 1 iff the first argument is constant.
 */
static int check_const_arg(arglist *al, const char *name) {
  if (al->dig && al->dig[0])
    return 1;
  /* Derivative information is requested, so the argument is not constant. */
  error(al, "argument '%s' is not constant", name);
  return 0;
}

/* Checks if the argument with the specified index is representable as int. */
static int check_int_arg(arglist *al, unsigned index, const char *name) {
  real arg = al->ra[index];
  if ((int)arg != arg) {
    error(al, "argument '%s' can't be represented as int, %s = %g",
        name, name, arg);
    return 0;
  }
  return 1;
}

/* Checks the arguments of a Bessel function. */
static int check_bessel_args(arglist *al, int flags) {
  int n = al->ra[0];
  if (!check_int_arg(al, 0, "n"))
    return 0;
  if (al->derivs) {
    int deriv_min = INT_MIN + ((flags & DERIV_INT_MIN) != 0 ? 0 : 1);
    if (!al->dig || !al->dig[0]) {
      /* Can't compute derivative with respect to an integer argument. */
      error(al, "argument 'n' is not constant");
      return 0;
    }
    if ((al->hes && !check_deriv_arg(al, n, INT_MIN + 2, INT_MAX - 2)) ||
        !check_deriv_arg(al, n, deriv_min, INT_MAX - 1))
      return 0;
  }
  return 1;
}

static real amplgsl_log1p(arglist *al) {
  real x = al->ra[0];
  if (x <= -1) {
    error(al, "argument 'x' should be > -1");
    return 0;
  }
  if (al->derivs) {
    real deriv = *al->derivs = 1 / (x + 1);
    if (al->hes)
      *al->hes = -deriv * deriv;
  }
  return gsl_log1p(x);
}

static real amplgsl_expm1(arglist *al) {
  real x = al->ra[0];
  if (al->derivs) {
    real deriv = *al->derivs = exp(x);
    if (al->hes)
      *al->hes = deriv;
  }
  return gsl_expm1(x);
}

static real amplgsl_hypot(arglist *al) {
  real x = al->ra[0];
  real y = al->ra[1];
  real hypot = gsl_hypot(x, y);
  if (al->derivs) {
    real *derivs = al->derivs;
    if (hypot == 0) {
      eval_error(al, "gsl_hypot'");
      return 0;
    }
    derivs[0] = x / hypot;
    derivs[1] = y / hypot;
    if (al->hes) {
      real *hes = al->hes;
      hes[0] =  derivs[1] * derivs[1] / hypot;
      hes[1] = -derivs[0] * derivs[1] / hypot;
      hes[2] =  derivs[0] * derivs[0] / hypot;
    }
  }
  return hypot;
}

static real amplgsl_hypot3(arglist *al) {
  real x = al->ra[0];
  real y = al->ra[1];
  real z = al->ra[2];
  real hypot = gsl_hypot3(x, y, z);
  if (al->derivs) {
    real *derivs = al->derivs;
    derivs[0] = x / hypot;
    derivs[1] = y / hypot;
    derivs[2] = z / hypot;
    if (al->hes) {
      real *hes = al->hes;
      real dx2 = derivs[0] * derivs[0];
      real dy2 = derivs[1] * derivs[1];
      real dz2 = derivs[2] * derivs[2];
      hes[0] =  (dy2 + dz2) / hypot;
      hes[1] = -derivs[0] * derivs[1] / hypot;
      hes[2] = -derivs[0] * derivs[2] / hypot;
      hes[3] =  (dx2 + dz2) / hypot;
      hes[4] = -derivs[1] * derivs[2] / hypot;
      hes[5] =  (dx2 + dy2) / hypot;
    }
  }
  return hypot;
}

static real amplgsl_sf_airy_Ai(arglist *al) {
  real x = al->ra[0];
  real value = gsl_sf_airy_Ai(x, GSL_PREC_DOUBLE);
  if (al->derivs) {
    *al->derivs = gsl_sf_airy_Ai_deriv(x, GSL_PREC_DOUBLE);
    if (al->hes)
      *al->hes = x * value;
  }
  return value;
}

static real amplgsl_sf_airy_Bi(arglist *al) {
  real x = al->ra[0];
  real value = gsl_sf_airy_Bi(x, GSL_PREC_DOUBLE);
  if (al->derivs) {
    *al->derivs = gsl_sf_airy_Bi_deriv(x, GSL_PREC_DOUBLE);
    if (al->hes)
      *al->hes = x * value;
  }
  return value;
}

static real amplgsl_sf_airy_Ai_scaled(arglist *al) {
  real x = al->ra[0];
  real value = gsl_sf_airy_Ai_scaled(x, GSL_PREC_DOUBLE);
  if (al->derivs) {
    if (x > 0) {
      real sqrtx = sqrt(x);
      *al->derivs = gsl_sf_airy_Ai_deriv_scaled(x, GSL_PREC_DOUBLE) +
          sqrtx * gsl_sf_airy_Ai_scaled(x, GSL_PREC_DOUBLE);
      if (al->hes)
        *al->hes = (value + 4 * x * *al->derivs) / (2 * sqrtx);
    } else {
      *al->derivs = gsl_sf_airy_Ai_deriv(x, GSL_PREC_DOUBLE);
      if (al->hes)
        *al->hes = x * value;
    }
  }
  return value;
}

static real amplgsl_sf_airy_Bi_scaled(arglist *al) {
  real x = al->ra[0];
  real value = gsl_sf_airy_Bi_scaled(x, GSL_PREC_DOUBLE);
  if (al->derivs) {
    if (x > 0) {
      real sqrtx = sqrt(x);
      *al->derivs = gsl_sf_airy_Bi_deriv_scaled(x, GSL_PREC_DOUBLE) -
          sqrtx * gsl_sf_airy_Bi_scaled(x, GSL_PREC_DOUBLE);
      if (al->hes)
        *al->hes = -(value + 4 * x * *al->derivs) / (2 * sqrtx);
    } else {
      *al->derivs = gsl_sf_airy_Bi_deriv(x, GSL_PREC_DOUBLE);
      if (al->hes)
        *al->hes = x * value;
    }
  }
  return value;
}

static real amplgsl_sf_airy_zero_Ai(arglist *al) {
  return check_zero_func_args(al, 0) ? gsl_sf_airy_zero_Ai(al->ra[0]) : 0;
}

static real amplgsl_sf_airy_zero_Bi(arglist *al) {
  return check_zero_func_args(al, 0) ? gsl_sf_airy_zero_Bi(al->ra[0]) : 0;
}

static real amplgsl_sf_airy_zero_Ai_deriv(arglist *al) {
  return check_zero_func_args(al, 0) ? gsl_sf_airy_zero_Ai_deriv(al->ra[0]) : 0;
}

static real amplgsl_sf_airy_zero_Bi_deriv(arglist *al) {
  return check_zero_func_args(al, 0) ? gsl_sf_airy_zero_Bi_deriv(al->ra[0]) : 0;
}

static real amplgsl_sf_bessel_J0(arglist *al) {
  real x = al->ra[0];
  real j0 = gsl_sf_bessel_J0(x);
  if (al->derivs) {
    *al->derivs = -gsl_sf_bessel_J1(x);
    if (al->hes)
      *al->hes = 0.5 * (gsl_sf_bessel_Jn(2, x) - j0);
  }
  return j0;
}

static real amplgsl_sf_bessel_J1(arglist *al) {
  real x = al->ra[0];
  real j1 = gsl_sf_bessel_J1(x);
  if (al->derivs) {
    *al->derivs = 0.5 * (gsl_sf_bessel_J0(x) - gsl_sf_bessel_Jn(2, x));
    if (al->hes)
      *al->hes = 0.25 * (gsl_sf_bessel_Jn(3, x) - 3 * j1);
  }
  return j1;
}

static real amplgsl_sf_bessel_Jn(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real jn = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  jn = gsl_sf_bessel_Jn(n, x);
  if (al->derivs) {
    al->derivs[1] = 0.5 *
        (gsl_sf_bessel_Jn(n - 1, x) - gsl_sf_bessel_Jn(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Jn(n - 2, x) - 2 * jn + gsl_sf_bessel_Jn(n + 2, x));
    }
  }
  return jn;
}

static real amplgsl_sf_bessel_Y0(arglist *al) {
  real x = al->ra[0];
  real y0 = gsl_sf_bessel_Y0(x);
  if (al->derivs) {
    *al->derivs = -gsl_sf_bessel_Y1(x);
    if (al->hes)
      *al->hes = 0.5 * (gsl_sf_bessel_Yn(2, x) - y0);
  }
  return check_result(al, y0, "gsl_sf_bessel_Y0");
}

static real amplgsl_sf_bessel_Y1(arglist *al) {
  real x = al->ra[0];
  real y1 = gsl_sf_bessel_Y1(x);
  if (al->derivs) {
    *al->derivs = 0.5 * (gsl_sf_bessel_Y0(x) - gsl_sf_bessel_Yn(2, x));
    if (al->hes)
      *al->hes = 0.25 * (gsl_sf_bessel_Yn(3, x) - 3 * y1);
  }
  return check_result(al, y1, "gsl_sf_bessel_Y1");
}

static real amplgsl_sf_bessel_Yn(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real yn = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  yn = gsl_sf_bessel_Yn(n, x);
  if (al->derivs) {
    al->derivs[1] = 0.5 *
        (gsl_sf_bessel_Yn(n - 1, x) - gsl_sf_bessel_Yn(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Yn(n - 2, x) - 2 * yn + gsl_sf_bessel_Yn(n + 2, x));
    }
  }
  return check_result(al, yn, "gsl_sf_bessel_Yn");
}

static real amplgsl_sf_bessel_I0(arglist *al) {
  real x = al->ra[0];
  real i0 = gsl_sf_bessel_I0(x);
  if (al->derivs) {
    *al->derivs = gsl_sf_bessel_I1(x);
    if (al->hes)
      *al->hes = 0.5 * (gsl_sf_bessel_In(2, x) + i0);
  }
  return i0;
}

static real amplgsl_sf_bessel_I1(arglist *al) {
  real x = al->ra[0];
  real i1 = gsl_sf_bessel_I1(x);
  if (al->derivs) {
    *al->derivs = 0.5 * (gsl_sf_bessel_I0(x) + gsl_sf_bessel_In(2, x));
    if (al->hes)
      *al->hes = 0.25 * (gsl_sf_bessel_In(3, x) + 3 * i1);
  }
  return i1;
}

static real amplgsl_sf_bessel_In(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real in = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  in = gsl_sf_bessel_In(n, x);
  if (al->derivs) {
    al->derivs[1] = 0.5 *
        (gsl_sf_bessel_In(n - 1, x) + gsl_sf_bessel_In(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_In(n - 2, x) + 2 * in + gsl_sf_bessel_In(n + 2, x));
    }
  }
  return in;
}

static real amplgsl_sf_bessel_I0_scaled(arglist *al) {
  real x = al->ra[0];
  real i0 = gsl_sf_bessel_I0_scaled(x);
  if (al->derivs) {
    real i1 = gsl_sf_bessel_I1_scaled(x);
    *al->derivs = i1 - mul_by_sign(x, i0);
    if (al->hes) {
      *al->hes = 1.5 * i0 - 2 * fabs(x) * i1 / x +
          0.5 * gsl_sf_bessel_In_scaled(2, x);
    }
  }
  return check_result(al, i0, "gsl_sf_bessel_I0_scaled");
}

static real amplgsl_sf_bessel_I1_scaled(arglist *al) {
  real x = al->ra[0];
  real i1 = gsl_sf_bessel_I1_scaled(x);
  if (al->derivs) {
    real i0 = gsl_sf_bessel_I0_scaled(x), i2 = gsl_sf_bessel_In_scaled(2, x);
    *al->derivs = 0.5 * i0 - mul_by_sign(x, i1) + 0.5 * i2;
    if (al->hes) {
      *al->hes = -fabs(x) * i0 / x + 1.75 * i1 - fabs(x) * i2 / x +
          0.25 * gsl_sf_bessel_In_scaled(3, x);
    }
  }
  return check_result(al, i1, "gsl_sf_bessel_I1_scaled");
}

static real amplgsl_sf_bessel_In_scaled(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real in = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  in = gsl_sf_bessel_In_scaled(n, x);
  if (al->derivs) {
    real in_minus_1 = gsl_sf_bessel_In_scaled(n - 1, x);
    real in_plus_1 = gsl_sf_bessel_In_scaled(n + 1, x);
    al->derivs[1] = 0.5 * in_minus_1 - mul_by_sign(x, in) + 0.5 * in_plus_1;
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_In_scaled(n - 2, x) + 6 * in +
           gsl_sf_bessel_In_scaled(n + 2, x)) -
           mul_by_sign(x, in_minus_1 + in_plus_1);
    }
  }
  return check_result(al, in, "gsl_sf_bessel_In_scaled");
}

static real amplgsl_sf_bessel_K0(arglist *al) {
  real x = al->ra[0];
  real k0 = gsl_sf_bessel_K0(x);
  if (al->derivs) {
    *al->derivs = -gsl_sf_bessel_K1(x);
    if (al->hes)
      *al->hes = 0.5 * (gsl_sf_bessel_Kn(2, x) + k0);
  }
  return check_result(al, k0, "gsl_sf_bessel_K0");
}

static real amplgsl_sf_bessel_K1(arglist *al) {
  real x = al->ra[0];
  real k1 = gsl_sf_bessel_K1(x);
  if (al->derivs) {
    *al->derivs = -0.5 * (gsl_sf_bessel_K0(x) + gsl_sf_bessel_Kn(2, x));
    if (al->hes)
      *al->hes = 0.25 * (gsl_sf_bessel_Kn(3, x) + 3 * k1);
  }
  return check_result(al, k1, "gsl_sf_bessel_K1");
}

static real amplgsl_sf_bessel_Kn(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real kn = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  kn = gsl_sf_bessel_Kn(n, x);
  if (al->derivs) {
    al->derivs[1] = -0.5 *
        (gsl_sf_bessel_Kn(n - 1, x) + gsl_sf_bessel_Kn(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Kn(n - 2, x) + 2 * kn + gsl_sf_bessel_Kn(n + 2, x));
    }
  }
  return check_result(al, kn, "gsl_sf_bessel_Kn");
}

static real amplgsl_sf_bessel_K0_scaled(arglist *al) {
  real x = al->ra[0];
  real k0 = gsl_sf_bessel_K0_scaled(x);
  if (al->derivs) {
    real k1 = gsl_sf_bessel_K1_scaled(x);
    *al->derivs = k0 - k1;
    if (al->hes)
      *al->hes = 1.5 * k0 - 2 * k1 + 0.5 * gsl_sf_bessel_Kn_scaled(2, x);
  }
  return check_result(al, k0, "gsl_sf_bessel_K0_scaled");
}

static real amplgsl_sf_bessel_K1_scaled(arglist *al) {
  real x = al->ra[0];
  real k1 = gsl_sf_bessel_K1_scaled(x);
  if (al->derivs) {
    real k0 = gsl_sf_bessel_K0_scaled(x), k2 = gsl_sf_bessel_Kn_scaled(2, x);
    *al->derivs = -0.5 * k0 + k1 - 0.5 * k2;
    if (al->hes)
      *al->hes = -k0 + 1.75 * k1 - k2 + 0.25 * gsl_sf_bessel_Kn_scaled(3, x);
  }
  return check_result(al, k1, "gsl_sf_bessel_K1_scaled");
}

static real amplgsl_sf_bessel_Kn_scaled(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real kn = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  kn = gsl_sf_bessel_Kn_scaled(n, x);
  if (al->derivs) {
    real kn_minus_1 = gsl_sf_bessel_Kn_scaled(n - 1, x);
    real kn_plus_1 = gsl_sf_bessel_Kn_scaled(n + 1, x);
    al->derivs[1] = -0.5 * (kn_minus_1 - 2 * kn + kn_plus_1);
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Kn_scaled(n - 2, x) - 4 * kn_minus_1 + 6 * kn -
              4 * kn_plus_1 + gsl_sf_bessel_Kn_scaled(n + 2, x));
    }
  }
  return check_result(al, kn, "gsl_sf_bessel_Kn_scaled");
}

static real amplgsl_sf_bessel_j0(arglist *al) {
  real x = al->ra[0];
  if (al->derivs) {
    *al->derivs = x != 0 ? (x * cos(x) - sin(x)) / gsl_pow_2(x) : 0;
    if (al->hes)
      *al->hes = ((2 - gsl_pow_2(x)) * sin(x) - 2 * x * cos(x)) / gsl_pow_3(x);
  }
  return check_result(al, gsl_sf_bessel_j0(x), "gsl_sf_bessel_j0");
}

static real amplgsl_sf_bessel_j1(arglist *al) {
  real x = al->ra[0];
  real j1 = gsl_sf_bessel_j1(x);
  if (al->derivs) {
    *al->derivs = x != 0 ? (sin(x) - 2 * j1) / x : 1.0 / 3.0;
    if (al->hes) {
      *al->hes = (x * (gsl_pow_2(x) - 6) * cos(x) -
          3 * (gsl_pow_2(x) - 2) * sin(x)) / gsl_pow_4(x);
    }
  }
  return check_result(al, j1, "gsl_sf_bessel_j1");
}

static real amplgsl_sf_bessel_j2(arglist *al) {
  real x = al->ra[0];
  real j2 = gsl_sf_bessel_j2(x);
  if (al->derivs) {
    *al->derivs = x != 0 ? gsl_sf_bessel_j1(x) - 3 * j2 / x : 0;
    if (al->hes) {
      *al->hes = (x * (5 * gsl_pow_2(x) - 36) * cos(x) +
          (gsl_pow_4(x) - 17 * gsl_pow_2(x) + 36) * sin(x)) / gsl_pow_5(x);
    }
  }
  return check_result(al, j2, "gsl_sf_bessel_j2");
}

static real amplgsl_sf_bessel_jl(arglist *al) {
  int el = al->ra[0];
  real x = al->ra[1];
  real jl = 0;
  if (!check_bessel_args(al, DERIV_INT_MIN))
    return 0;
  jl = gsl_sf_bessel_jl(el, x);
  if (al->derivs) {
    real jn_plus_1 = gsl_sf_bessel_jl(el + 1, x);
    if (x == 0)
      al->derivs[1] = el == 1 ? 1.0 / 3.0 : 0;
    else
      al->derivs[1] = el * jl / x - jn_plus_1;
    if (al->hes) {
      al->hes[2] = (
          gsl_pow_2(x) * gsl_sf_bessel_jl(el - 2, x) -
          2 * x * gsl_sf_bessel_jl(el - 1, x) -
          (2 * gsl_pow_2(x) - 3) * jl + 2 * x * jn_plus_1 +
          gsl_pow_2(x) * gsl_sf_bessel_jl(el + 2, x)) / (4 * gsl_pow_2(x));
    }
  }
  return check_result(al, jl, "gsl_sf_bessel_jl");
}

static real amplgsl_sf_bessel_y0(arglist *al) {
  real x = al->ra[0];
  if (al->derivs) {
    *al->derivs = (x * sin(x) + cos(x)) / gsl_pow_2(x);
    if (al->hes) {
      *al->hes = ((gsl_pow_2(x) - 2) * cos(x) - 2 * x * sin(x)) /
          gsl_pow_3(x);
    }
  }
  return check_result(al, gsl_sf_bessel_y0(x), "gsl_sf_bessel_y0");
}

static real amplgsl_sf_bessel_y1(arglist *al) {
  real x = al->ra[0];
  real y1 = gsl_sf_bessel_y1(x);
  if (al->derivs) {
    *al->derivs = -(2 * y1 + cos(x)) / x;
    if (al->hes) {
      *al->hes = (x * (gsl_pow_2(x) - 6) * sin(x) +
          3 * (gsl_pow_2(x) - 2) * cos(x)) / gsl_pow_4(x);
    }
  }
  return check_result(al, y1, "gsl_sf_bessel_y1");
}

static real amplgsl_sf_bessel_y2(arglist *al) {
  real x = al->ra[0];
  real y2 = gsl_sf_bessel_y2(x);
  if (al->derivs) {
    real y1 = gsl_sf_bessel_y1(x);
    *al->derivs = y1 - (3 * y2) / x;
    if (al->hes) {
      *al->hes = ((36 - 5 * gsl_pow_2(x)) * y1 -
          (gsl_pow_2(x) - 12) * cos(x)) / gsl_pow_3(x);
    }
  }
  return check_result(al, y2, "gsl_sf_bessel_y2");
}

static real amplgsl_sf_bessel_yl(arglist *al) {
  int el = al->ra[0];
  real x = al->ra[1];
  real yl = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  yl = gsl_sf_bessel_yl(el, x);
  if (al->derivs) {
    real yn_minus_1 = el != 0 ? gsl_sf_bessel_yl(el - 1, x) : sin(x) / x;
    real yn_plus_1 = gsl_sf_bessel_yl(el + 1, x);
    al->derivs[1] = 0.5 * (yn_minus_1 - yl / x - yn_plus_1);
    if (al->hes) {
      al->hes[2] = (
          gsl_pow_2(x) * gsl_sf_bessel_yl(el - 2, x) - 2 * x * yn_minus_1 -
          (2 * gsl_pow_2(x) - 3) * yl + 2 * x * yn_plus_1 +
          gsl_pow_2(x) * gsl_sf_bessel_yl(el + 2, x)) / (4 * gsl_pow_2(x));
    }
  }
  return check_result(al, yl, "gsl_sf_bessel_yl");
}

static real amplgsl_sf_bessel_i0_scaled(arglist *al) {
  real x = al->ra[0];
  real i0 = gsl_sf_bessel_i0_scaled(x);
  if (al->derivs) {
    /* Contrary to the documentation, gsl_sf_bessel_i0_scaled
       implements \exp(-|x|) \sqrt{\pi}/\sqrt{2x} I_{1/2}(x)
       and not \exp(-|x|) \sqrt{\pi/(2x)} I_{1/2}(x).
       These are different since \sqrt(1/x) != \sqrt(x) for negative x. */
    real hyp_coef = exp(-fabs(x)) / x;
    real i_minus_1 = hyp_coef * cosh(x);
    real i1 = gsl_sf_bessel_i1_scaled(x);
    real coef = -(1 + 2 * fabs(x)) / x;
    *al->derivs = 0.5 * (i_minus_1 + coef * i0 + i1);
    if (!check_result(al, *al->derivs, "gsl_sf_bessel_i0_scaled'"))
      return 0;
    if (al->hes) {
      coef *= 2;
      *al->hes = 0.25 * (
          hyp_coef * sinh(x) - i_minus_1 / x +
          coef * i_minus_1 +
          (3 + 6 * gsl_pow_2(x) + 4 * fabs(x)) * i0 / gsl_pow_2(x) +
          coef * i1 +
          gsl_sf_bessel_il_scaled(2, x));
    }
  }
  return check_result(al, i0, "gsl_sf_bessel_i0_scaled");
}

static real amplgsl_sf_bessel_i1_scaled(arglist *al) {
  real x = al->ra[0];
  real i1 = gsl_sf_bessel_i1_scaled(x);
  if (al->derivs) {
    /* Contrary to the documentation, gsl_sf_bessel_i1_scaled
       implements \exp(-|x|) \sqrt{\pi}/\sqrt{2x} I_{1+1/2}(x)
       and not \exp(-|x|) \sqrt{\pi/(2x)} I_{1+1/2}(x).
       These are different since \sqrt(1/x) != \sqrt(x) for negative x. */
    real i0 = gsl_sf_bessel_i0_scaled(x);
    real i2 = gsl_sf_bessel_i2_scaled(x);
    real coef = -(1 + 2 * fabs(x)) / x;
    *al->derivs = x != 0 ? 0.5 * (i0 + coef * i1 + i2) : 1.0 / 3.0;
    if (al->hes) {
      coef *= 2;
      *al->hes = 0.25 * (
          exp(-fabs(x)) * sqrt(1 / x) * cosh(x) / sqrt(x) +
          coef * i0 +
          (3 + 6 * gsl_pow_2(x) + 4 * fabs(x)) * i1 / gsl_pow_2(x) +
          coef * i2 +
          gsl_sf_bessel_il_scaled(3, x));
    }
  }
  return check_result(al, i1, "gsl_sf_bessel_i1_scaled");
}

static real amplgsl_sf_bessel_i2_scaled(arglist *al) {
  real x = al->ra[0];
  real i2 = gsl_sf_bessel_i2_scaled(x);
  if (al->derivs) {
    /* Contrary to the documentation, gsl_sf_bessel_i2_scaled
       implements \exp(-|x|) \sqrt{\pi}/\sqrt{2x} I_{2+1/2}(x)
       and not \exp(-|x|) \sqrt{\pi/(2x)} I_{2+1/2}(x).
       These are different since \sqrt(1/x) != \sqrt(x) for negative x. */
    real i1 = gsl_sf_bessel_i1_scaled(x);
    real i3 = gsl_sf_bessel_il_scaled(3, x);
    real coef = -(1 + 2 * fabs(x)) / x;
    *al->derivs = x != 0 ? 0.5 * (i1 + coef * i2 + i3) : 0;
    if (al->hes) {
      coef *= 2;
      *al->hes = 0.25 * (
          gsl_sf_bessel_i0_scaled(x) +
          coef * i1 +
          (3 + 6 * gsl_pow_2(x) + 4 * fabs(x)) * i2 / gsl_pow_2(x) +
          coef * i3 +
          gsl_sf_bessel_il_scaled(4, x));
    }
  }
  return check_result(al, i2, "gsl_sf_bessel_i2_scaled");
}

static real amplgsl_sf_bessel_il_scaled(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real in = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  in = gsl_sf_bessel_il_scaled(n, x);
  if (al->derivs) {
    real in_minus_1 = n != 0 ?
        gsl_sf_bessel_il_scaled(n - 1, x) : exp(-fabs(x)) * cosh(x) / x;
    real in_plus_1 = gsl_sf_bessel_il_scaled(n + 1, x);
    real coef = -(1 + 2 * fabs(x)) / x;
    real deriv = GSL_NAN;
    if (x == 0) {
      if (n == 1)
        deriv = 1.0 / 3.0;
      else if (n > 1)
        deriv = 0;
    }
    if (gsl_isnan(deriv))
      deriv = 0.5 * (in_minus_1 + coef * in + in_plus_1);
    al->derivs[1] = deriv;
    if (al->hes) {
      coef *= 2;
      al->hes[2] = 0.25 * (
          gsl_sf_bessel_il_scaled(n - 2, x) +
          coef * in_minus_1 +
          (3 + 4 * fabs(x) + 6 * gsl_pow_2(x)) * in / gsl_pow_2(x) +
          coef * in_plus_1 +
          gsl_sf_bessel_il_scaled(n + 2, x));
    }
  }
  return check_result(al, in, "gsl_sf_bessel_il_scaled");
}

static real amplgsl_sf_bessel_k0_scaled(arglist *al) {
  real x = al->ra[0];
  real k0 = gsl_sf_bessel_k0_scaled(x);
  if (al->derivs) {
    real pi_sqrt_inv_x = M_PI * sqrt(1 / x);
    *al->derivs = -pi_sqrt_inv_x / (2 * pow(x, 1.5));
    if (al->hes)
      *al->hes = pi_sqrt_inv_x / pow(x, 2.5);
  }
  return check_result(al, k0, "gsl_sf_bessel_k0_scaled");
}

static real amplgsl_sf_bessel_k1_scaled(arglist *al) {
  real x = al->ra[0];
  if (al->derivs) {
    real pi_sqrt_inv_x = M_PI * sqrt(1 / x);
    *al->derivs = -(pi_sqrt_inv_x * (x + 2)) / (2 * pow(x, 2.5));
    if (al->hes)
      *al->hes = (pi_sqrt_inv_x * (x + 3)) / pow(x, 3.5);
  }
  return check_result(al, gsl_sf_bessel_k1_scaled(x),
      "gsl_sf_bessel_k1_scaled");
}

static real amplgsl_sf_bessel_k2_scaled(arglist *al) {
  real x = al->ra[0];
  if (al->derivs) {
    real pi_sqrt_inv_x = M_PI * sqrt(1 / x);
    *al->derivs = -pi_sqrt_inv_x * (x + 3) * (x + 3) / (2 * pow(x, 3.5));
    if (al->hes)
      *al->hes = pi_sqrt_inv_x * (x * x + 9 * x + 18) / pow(x, 4.5);
  }
  return check_result(al, gsl_sf_bessel_k2_scaled(x),
      "gsl_sf_bessel_k2_scaled");
}

static real amplgsl_sf_bessel_kl_scaled(arglist *al) {
  int n = al->ra[0];
  real x = al->ra[1];
  real kn = 0;
  if (!check_bessel_args(al, 0))
    return 0;
  kn = gsl_sf_bessel_kl_scaled(n, x);
  if (al->derivs) {
    real kn_minus_1 = n != 0 ? gsl_sf_bessel_kl_scaled(n - 1, x) : M_PI_2 / x;
    real kn_plus_1 = gsl_sf_bessel_kl_scaled(n + 1, x);
    real coef = (1 - 2 * x) / x;
    al->derivs[1] = -0.5 * (kn_minus_1 + coef * kn + kn_plus_1);
    if (al->hes) {
      coef *= 2;
      al->hes[2] = 0.25 * (
          gsl_sf_bessel_kl_scaled(n - 2, x) +
          coef * kn_minus_1 +
          (3 - 4 * x + 6 * gsl_pow_2(x)) * kn / gsl_pow_2(x) +
          coef * kn_plus_1 +
          gsl_sf_bessel_kl_scaled(n + 2, x));
    }
  }
  return check_result(al, kn, "gsl_sf_bessel_kl_scaled");
}

static real amplgsl_sf_bessel_Jnu(arglist *al) {
  real n = al->ra[0];
  real x = al->ra[1];
  real jn = gsl_sf_bessel_Jnu(n, x);
  if (al->derivs) {
    if (!check_const_arg(al, "nu"))
      return 0;
    al->derivs[1] = 0.5 *
        (gsl_sf_bessel_Jnu(n - 1, x) - gsl_sf_bessel_Jnu(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Jnu(n - 2, x) - 2 * jn + gsl_sf_bessel_Jnu(n + 2, x));
    }
  }
  return check_result(al, jn, "gsl_sf_bessel_Jnu");
}

static real amplgsl_sf_bessel_Ynu(arglist *al) {
  real n = al->ra[0];
  real x = al->ra[1];
  real yn = gsl_sf_bessel_Ynu(n, x);
  if (al->derivs) {
    if (!check_const_arg(al, "nu"))
      return 0;
    al->derivs[1] = 0.5 *
        (gsl_sf_bessel_Ynu(n - 1, x) - gsl_sf_bessel_Ynu(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Ynu(n - 2, x) - 2 * yn + gsl_sf_bessel_Ynu(n + 2, x));
    }
  }
  return check_result(al, yn, "gsl_sf_bessel_Ynu");
}

static real amplgsl_sf_bessel_Inu(arglist *al) {
  real n = al->ra[0];
  real x = al->ra[1];
  real in = gsl_sf_bessel_Inu(n, x);
  if (al->derivs) {
    if (!check_const_arg(al, "nu"))
      return 0;
    al->derivs[1] = 0.5 *
        (gsl_sf_bessel_Inu(n - 1, x) + gsl_sf_bessel_Inu(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Inu(n - 2, x) + 2 * in + gsl_sf_bessel_Inu(n + 2, x));
    }
  }
  return check_result(al, in, "gsl_sf_bessel_Inu");
}

static real amplgsl_sf_bessel_Inu_scaled(arglist *al) {
  real n = al->ra[0];
  real x = al->ra[1];
  real in = gsl_sf_bessel_Inu_scaled(n, x);
  if (al->derivs) {
    real in_minus_1 = 0, in_plus_1 = 0;
    if (!check_const_arg(al, "nu"))
      return 0;
    in_minus_1 = gsl_sf_bessel_Inu_scaled(n - 1, x);
    in_plus_1 = gsl_sf_bessel_Inu_scaled(n + 1, x);
    al->derivs[1] = 0.5 * in_minus_1 - mul_by_sign(x, in) + 0.5 * in_plus_1;
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Inu_scaled(n - 2, x) + 6 * in +
              gsl_sf_bessel_Inu_scaled(n + 2, x)) -
              mul_by_sign(x, in_minus_1 + in_plus_1);
    }
  }
  return check_result(al, in, "gsl_sf_bessel_Inu_scaled");
}

static real amplgsl_sf_bessel_Knu(arglist *al) {
  real n = al->ra[0];
  real x = al->ra[1];
  real kn = gsl_sf_bessel_Knu(n, x);
  if (al->derivs) {
    if (!check_const_arg(al, "nu"))
      return 0;
    al->derivs[1] = -0.5 *
        (gsl_sf_bessel_Knu(n - 1, x) + gsl_sf_bessel_Knu(n + 1, x));
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Knu(n - 2, x) + 2 * kn + gsl_sf_bessel_Knu(n + 2, x));
    }
  }
  return check_result(al, kn, "gsl_sf_bessel_Knu");
}

static real amplgsl_sf_bessel_lnKnu(arglist *al) {
  real n = al->ra[0];
  real x = al->ra[1];
  if (al->derivs) {
    real kn = 0, kn_minus_1_plus_1 = 0;
    if (!check_const_arg(al, "nu"))
      return 0;
    kn = gsl_sf_bessel_Knu(n, x);
    kn_minus_1_plus_1 =
        gsl_sf_bessel_Knu(n - 1, x) + gsl_sf_bessel_Knu(n + 1, x);
    al->derivs[1] = -0.5 * kn_minus_1_plus_1 / kn;
    if (al->hes) {
      al->hes[2] = 0.25 *
          (kn * (gsl_sf_bessel_Knu(n - 2, x) + 2 * kn +
          gsl_sf_bessel_Knu(n + 2, x)) -
              kn_minus_1_plus_1 * kn_minus_1_plus_1) / (kn * kn);
    }
  }
  return check_result(al, gsl_sf_bessel_lnKnu(n, x), "gsl_sf_bessel_lnKnu");
}

static real amplgsl_sf_bessel_Knu_scaled(arglist *al) {
  real n = al->ra[0];
  real x = al->ra[1];
  real kn = gsl_sf_bessel_Knu_scaled(n, x);
  if (al->derivs) {
    real kn_minus_1 = 0, kn_plus_1 = 0;
    if (!check_const_arg(al, "nu"))
      return 0;
    kn_minus_1 = gsl_sf_bessel_Knu_scaled(n - 1, x);
    kn_plus_1 = gsl_sf_bessel_Knu_scaled(n + 1, x);
    al->derivs[1] = -0.5 * (kn_minus_1 - 2 * kn + kn_plus_1);
    if (al->hes) {
      al->hes[2] = 0.25 *
          (gsl_sf_bessel_Knu_scaled(n - 2, x) - 4 * kn_minus_1 + 6 * kn -
              4 * kn_plus_1 + gsl_sf_bessel_Knu_scaled(n + 2, x));
    }
  }
  return check_result(al, kn, "gsl_sf_bessel_Knu_scaled");
}

static real amplgsl_sf_bessel_zero_J0(arglist *al) {
  return check_zero_func_args(al, 0) ? gsl_sf_bessel_zero_J0(al->ra[0]) : 0;
}

static real amplgsl_sf_bessel_zero_J1(arglist *al) {
  return check_zero_func_args(al, 0) ? gsl_sf_bessel_zero_J1(al->ra[0]) : 0;
}

static real amplgsl_sf_bessel_zero_Jnu(arglist *al) {
  return check_zero_func_args(al, 1) ?
      gsl_sf_bessel_zero_Jnu(al->ra[0], al->ra[1]) : 0;
}

static real amplgsl_sf_clausen(arglist *al) {
  real x = al->ra[0];
  if (al->derivs) {
    *al->derivs = -log(2 * sin(0.5 * fabs(x)));
    if (al->hes)
      *al->hes = -0.5 * tan(0.5 * M_PI - x);
  }
  return gsl_sf_clausen(x);
}

static real amplgsl_sf_hydrogenicR_1(arglist *al) {
  real Z = al->ra[0], r = al->ra[1];
  if (al->derivs) {
    real *derivs = al->derivs;
    real exp_minusZr = exp(-Z * r);
    derivs[0] = sqrt(Z) * exp_minusZr * (3 - 2 * r * Z);
    derivs[1] = -2 * pow(Z, 2.5) * exp_minusZr;
    if (al->hes) {
      real *hes = al->hes;
      hes[0] = (exp_minusZr * (4 * gsl_pow_2(r * Z) - 12 * r * Z + 3)) /
          (2 *sqrt(Z));
      hes[1] = pow(Z, 1.5) * exp_minusZr * (2 * r * Z - 5);
      hes[2] = 2 * pow(Z, 3.5) * exp_minusZr;
    }
  }
  return check_result(al, gsl_sf_hydrogenicR_1(Z, r), "gsl_sf_hydrogenicR_1");
}

static real amplgsl_sf_hydrogenicR(arglist *al) {
  if (!check_int_arg(al, 0, "n") || !check_int_arg(al, 1, "l"))
    return 0;
  if (al->derivs) {
    error(al, "derivative is not provided");
    return 0;
  }
  return gsl_sf_hydrogenicR(al->ra[0], al->ra[1], al->ra[2], al->ra[3]);
}

static real amplgsl_sf_coulomb_CL(arglist *al) {
  gsl_sf_result result = {0};
  if (al->derivs) {
    error(al, "derivative is not provided");
    return 0;
  }
  return gsl_sf_coulomb_CL_e(al->ra[0], al->ra[1], &result) ?
      GSL_NAN : result.val;
}

static int check_coupling_args(arglist *al, const char *const* arg_names) {
  unsigned i = 0, n_args = al->n;
  for (; i < n_args; ++i) {
    if (!check_int_arg(al, i, arg_names[i]))
      return 0;
  }
  if (al->derivs) {
    error(al, "arguments are not constant");
    return 0;
  }
  return 1;
}

static real amplgsl_sf_coupling_3j(arglist *al) {
  static const char *ARG_NAMES[] = {
      "two_ja", "two_jb", "two_jc",
      "two_ma", "two_mb", "two_mc"
  };
  if (!check_coupling_args(al, ARG_NAMES))
    return 0;
  return gsl_sf_coupling_3j(
      al->ra[0], al->ra[1], al->ra[2], al->ra[3], al->ra[4], al->ra[5]);
}

static real amplgsl_sf_coupling_6j(arglist *al) {
  static const char *ARG_NAMES[] = {
      "two_ja", "two_jb", "two_jc",
      "two_jd", "two_je", "two_jf"
  };
  if (!check_coupling_args(al, ARG_NAMES))
    return 0;
  return gsl_sf_coupling_6j(
      al->ra[0], al->ra[1], al->ra[2], al->ra[3], al->ra[4], al->ra[5]);
}

static real amplgsl_sf_coupling_9j(arglist *al) {
  static const char *ARG_NAMES[] = {
      "two_ja", "two_jb", "two_jc",
      "two_jd", "two_je", "two_jf",
      "two_jg", "two_jh", "two_ji"
  };
  if (!check_coupling_args(al, ARG_NAMES))
    return 0;
  return gsl_sf_coupling_9j(
      al->ra[0], al->ra[1], al->ra[2], al->ra[3], al->ra[4], al->ra[5],
      al->ra[6], al->ra[7], al->ra[8]);
}

static real amplgsl_sf_dawson(arglist *al) {
  real x = al->ra[0];
  real f = gsl_sf_dawson(x);
  if (al->derivs) {
    real deriv = *al->derivs = 1 - 2 * x * f;
    if (al->hes)
      *al->hes = - 2 * (f + x * deriv);
  }
  return f;
}

/* Values of the derivatives of the Debye functions at 0. */
static const double DEBYE_DERIV_AT_0[] = {
    -1.0 / 4.0, -1.0 / 3.0,  -3.0 / 8.0,
    -2.0 / 5.0, -5.0 / 12.0, -3.0 / 7.0
};

static real debye(arglist *al, int n,
    double (*func)(double), const char *name) {
  real x = al->ra[0];
  real f = func(x);
  if (al->derivs) {
    real exp_x = exp(x);
    real deriv = *al->derivs = x != 0 ?
        n * (1 / (exp_x - 1) - f / x) : DEBYE_DERIV_AT_0[n - 1];
    if (al->hes) {
      *al->hes = n * (-exp_x / gsl_pow_2(exp_x - 1) +
          f / gsl_pow_2(x) - deriv / x);
    }
  }
  return check_result(al, f, name);
}

#define DEBYE(n) \
  static real amplgsl_sf_debye_##n(arglist *al) { \
    return debye(al, n, gsl_sf_debye_##n, "gsl_sf_debye_" #n); \
  }

DEBYE(1)
DEBYE(2)
DEBYE(3)
DEBYE(4)
DEBYE(5)
DEBYE(6)

static real amplgsl_sf_dilog(arglist *al) {
  real x = al->ra[0];
  if (al->derivs) {
    gsl_complex log = gsl_complex_log(gsl_complex_rect(1 - x, 0));
    real deriv = *al->derivs = x != 0 ? -GSL_REAL(log) / x : 1;
    if (al->hes)
      *al->hes = x != 0 ? (1 / (1 - x) - deriv) / x : 0.5;
  }
  return gsl_sf_dilog(x);
}

static real amplgsl_sf_ellint_Kcomp(arglist *al) {
  real k = al->ra[0];
  real kcomp = gsl_sf_ellint_Kcomp(k, GSL_PREC_DOUBLE);
  if (al->derivs) {
    real ecomp = gsl_sf_ellint_Ecomp(k, GSL_PREC_DOUBLE);
    real divisor = k * (1 - k * k);
    *al->derivs = k != 0 ? ecomp / divisor - kcomp / k : 0;
    if (al->hes) {
      *al->hes = k != 0 ?
          ((2 * gsl_pow_4(k) - 3 * gsl_pow_2(k) + 1) * kcomp +
          (3 * gsl_pow_2(k) - 1) * ecomp) / gsl_pow_2(divisor) : M_PI_4;
    }
  }
  return check_result(al, kcomp, "gsl_sf_ellint_Kcomp");
}

static real amplgsl_sf_ellint_Ecomp(arglist *al) {
  real k = al->ra[0];
  real ecomp = gsl_sf_ellint_Ecomp(k, GSL_PREC_DOUBLE);
  if (al->derivs) {
    real kcomp = gsl_sf_ellint_Kcomp(k, GSL_PREC_DOUBLE);
    *al->derivs = k != 0 ? (ecomp - kcomp) / k : 0;
    if (al->hes) {
      *al->hes = k != 0 ?
          ((k * k - 1) * kcomp + ecomp) / (k * k * (k * k - 1)) : -M_PI_4;
    }
  }
  return check_result(al, ecomp, "gsl_sf_ellint_Ecomp");
}

inline double pow2(double x) { return x * x; }

static real amplgsl_sf_ellint_Pcomp(arglist *al) {
  real k = al->ra[0], n = al->ra[1];
  real pcomp = gsl_sf_ellint_Pcomp(k, n, GSL_PREC_DOUBLE);
  if (al->derivs) {
    real ecomp = gsl_sf_ellint_Ecomp(k, GSL_PREC_DOUBLE);
    real kcomp = gsl_sf_ellint_Kcomp(k, GSL_PREC_DOUBLE);
    real divisor = (k * k - 1) * (k * k + n);
    al->derivs[0] = -k * ((k * k - 1) * pcomp + ecomp) / divisor;
    al->derivs[1] = (-kcomp * (k * k + n) +
        (k * k - n * n) * pcomp + n * ecomp) / (2 * n * (n + 1) * (k * k + n));
    if (al->hes) {
      al->hes[0] = ((k * k - 1) * (kcomp * (k * k + n) +
          (k * k - 1) * (2 * k * k - n) * pcomp) +
          (3 * gsl_pow_4(k) - k * k + 2 * n) * ecomp) / gsl_pow_2(divisor);
      al->hes[1] = (k * ((k * k - 1) * (kcomp * (k * k + n) +
          (n * (3 * n + 2) - k * k) * pcomp) +
          n * (-k * k + 2 * n + 3) * ecomp)) /
          (2 * divisor * n * (n + 1) * (k * k + n));
      al->hes[2] = (kcomp * (gsl_pow_4(k) * (4 * n + 1) +
          3 * k * k * n * (3 * n + 1) + n * n * (5 * n + 2)) +
          n * (k * k * (1 - 2 * n) - n * (5 * n + 2)) * ecomp -
          (gsl_pow_4(k) * (4 * n + 1) + 2 * k * k * n * (5 * n + 2) -
              3 * gsl_pow_4(n)) * pcomp) /
              (4 * gsl_pow_2(n * (n + 1) * (k * k + n)));
    }
  }
  return pcomp;
}

static real amplgsl_sf_ellint_F(arglist *al) {
  real phi = al->ra[0], k = al->ra[1];
  real f = gsl_sf_ellint_F(phi, k, GSL_PREC_DOUBLE);
  if (al->derivs) {
    real e = gsl_sf_ellint_E(phi, k, GSL_PREC_DOUBLE);
    al->derivs[0] = 1 / sqrt(1 - gsl_pow_2(k * sin(phi)));
    al->derivs[1] = (e + (k * k - 1) * f -
        (k * k * cos(phi) * sin(phi)) / sqrt(1 - gsl_pow_2(k * sin(phi)))) /
        (k - gsl_pow_3(k));
    if (al->hes) {
      real coef = pow(2 - k * k + k * k * cos(2 * phi), 1.5);
      al->hes[0] = (k * k * sin(phi) * cos(phi)) /
          pow(1 - gsl_pow_2(k * sin(phi)), 1.5);
      al->hes[1] = (2 * M_SQRT2 * k * gsl_pow_2(sin(phi))) /
          pow(k * k * cos(2 * phi) - k * k + 2, 1.5);
      al->hes[2] = -(-M_SQRT2 * (3 * k * k - 1) * coef * e -
          M_SQRT2 * (1 - 3 * k * k + 2 * gsl_pow_4(k)) * coef * f +
        4 * gsl_pow_4(k) * ((1 - 3 * k * k) * cos(phi) * gsl_pow_3(sin(phi)) +
            sin(2 * phi))) / (M_SQRT2 * gsl_pow_2(k * (k * k - 1)) * coef);
    }
  }
  return f;
}

void funcadd_ASL(AmplExports *ae) {
  /* Don't call abort on error. */
  gsl_set_error_handler_off();

  /* Elementary Functions */
  addfunc("gsl_log1p", amplgsl_log1p, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_expm1", amplgsl_expm1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_hypot", amplgsl_hypot, FUNCADD_REAL_VALUED, 2, 0);
  addfunc("gsl_hypot3", amplgsl_hypot3, FUNCADD_REAL_VALUED, 3, 0);

  /* AMPL has built-in functions acosh, asinh and atanh so wrappers
     are not provided for their GSL equivalents. */

  /* Wrappers for functions operating on complex numbers are not provided
     since this requires support for structures/tuples as function arguments. */

  /* Airy Functions */
  addfunc("gsl_sf_airy_Ai", amplgsl_sf_airy_Ai, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_airy_Bi", amplgsl_sf_airy_Bi, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_airy_Ai_scaled", amplgsl_sf_airy_Ai_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_airy_Bi_scaled", amplgsl_sf_airy_Bi_scaled,
      FUNCADD_REAL_VALUED, 1, 0);

  /* Zeros of Airy Functions */
  addfunc("gsl_sf_airy_zero_Ai", amplgsl_sf_airy_zero_Ai,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_airy_zero_Bi", amplgsl_sf_airy_zero_Bi,
      FUNCADD_REAL_VALUED, 1, 0);

  /* Zeros of Derivatives of Airy Functions */
  addfunc("gsl_sf_airy_zero_Ai_deriv", amplgsl_sf_airy_zero_Ai_deriv,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_airy_zero_Bi_deriv", amplgsl_sf_airy_zero_Bi_deriv,
      FUNCADD_REAL_VALUED, 1, 0);

  /* Bessel Functions */
  addfunc("gsl_sf_bessel_J0", amplgsl_sf_bessel_J0, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_J1", amplgsl_sf_bessel_J1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_Jn", amplgsl_sf_bessel_Jn, FUNCADD_REAL_VALUED, 2, 0);

  /* Irregular Cylindrical Bessel Functions */
  addfunc("gsl_sf_bessel_Y0", amplgsl_sf_bessel_Y0, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_Y1", amplgsl_sf_bessel_Y1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_Yn", amplgsl_sf_bessel_Yn, FUNCADD_REAL_VALUED, 2, 0);

  /* Regular Modified Cylindrical Bessel Functions */
  addfunc("gsl_sf_bessel_I0", amplgsl_sf_bessel_I0, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_I1", amplgsl_sf_bessel_I1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_In", amplgsl_sf_bessel_In, FUNCADD_REAL_VALUED, 2, 0);
  addfunc("gsl_sf_bessel_I0_scaled", amplgsl_sf_bessel_I0_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_I1_scaled", amplgsl_sf_bessel_I1_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_In_scaled", amplgsl_sf_bessel_In_scaled,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Irregular Modified Cylindrical Bessel Functions */
  addfunc("gsl_sf_bessel_K0", amplgsl_sf_bessel_K0, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_K1", amplgsl_sf_bessel_K1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_Kn", amplgsl_sf_bessel_Kn, FUNCADD_REAL_VALUED, 2, 0);
  addfunc("gsl_sf_bessel_K0_scaled", amplgsl_sf_bessel_K0_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_K1_scaled", amplgsl_sf_bessel_K1_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_Kn_scaled", amplgsl_sf_bessel_Kn_scaled,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Regular Spherical Bessel Functions */
  addfunc("gsl_sf_bessel_j0", amplgsl_sf_bessel_j0, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_j1", amplgsl_sf_bessel_j1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_j2", amplgsl_sf_bessel_j2, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_jl", amplgsl_sf_bessel_jl, FUNCADD_REAL_VALUED, 2, 0);

  /* Irregular Spherical Bessel Functions */
  addfunc("gsl_sf_bessel_y0", amplgsl_sf_bessel_y0, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_y1", amplgsl_sf_bessel_y1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_y2", amplgsl_sf_bessel_y2, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_yl", amplgsl_sf_bessel_yl, FUNCADD_REAL_VALUED, 2, 0);

  /* Regular Modified Spherical Bessel Functions */
  addfunc("gsl_sf_bessel_i0_scaled", amplgsl_sf_bessel_i0_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_i1_scaled", amplgsl_sf_bessel_i1_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_i2_scaled", amplgsl_sf_bessel_i2_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_il_scaled", amplgsl_sf_bessel_il_scaled,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Irregular Modified Spherical Bessel Functions */
  addfunc("gsl_sf_bessel_k0_scaled", amplgsl_sf_bessel_k0_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_k1_scaled", amplgsl_sf_bessel_k1_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_k2_scaled", amplgsl_sf_bessel_k2_scaled,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_kl_scaled", amplgsl_sf_bessel_kl_scaled,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Regular Bessel Function - Fractional Order */
  addfunc("gsl_sf_bessel_Jnu", amplgsl_sf_bessel_Jnu,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Irregular Bessel Functions - Fractional Order */
  addfunc("gsl_sf_bessel_Ynu", amplgsl_sf_bessel_Ynu,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Regular Modified Bessel Functions - Fractional Order */
  addfunc("gsl_sf_bessel_Inu", amplgsl_sf_bessel_Inu,
      FUNCADD_REAL_VALUED, 2, 0);
  addfunc("gsl_sf_bessel_Inu_scaled", amplgsl_sf_bessel_Inu_scaled,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Irregular Modified Bessel Functions - Fractional Order */
  addfunc("gsl_sf_bessel_Knu", amplgsl_sf_bessel_Knu,
      FUNCADD_REAL_VALUED, 2, 0);
  addfunc("gsl_sf_bessel_lnKnu", amplgsl_sf_bessel_lnKnu,
      FUNCADD_REAL_VALUED, 2, 0);
  addfunc("gsl_sf_bessel_Knu_scaled", amplgsl_sf_bessel_Knu_scaled,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Zeros of Regular Bessel Functions */
  addfunc("gsl_sf_bessel_zero_J0", amplgsl_sf_bessel_zero_J0,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_zero_J1", amplgsl_sf_bessel_zero_J1,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_bessel_zero_Jnu", amplgsl_sf_bessel_zero_Jnu,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Clausen Function */
  addfunc("gsl_sf_clausen", amplgsl_sf_clausen, FUNCADD_REAL_VALUED, 1, 0);

  /* Normalized Hydrogenic Bound States */
  addfunc("gsl_sf_hydrogenicR_1", amplgsl_sf_hydrogenicR_1,
      FUNCADD_REAL_VALUED, 2, 0);
  addfunc("gsl_sf_hydrogenicR", amplgsl_sf_hydrogenicR,
      FUNCADD_REAL_VALUED, 4, 0);

  /* Coulomb Wave Function Normalization Constant */
  addfunc("gsl_sf_coulomb_CL", amplgsl_sf_coulomb_CL,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Coupling Coefficients */
  addfunc("gsl_sf_coupling_3j", amplgsl_sf_coupling_3j,
      FUNCADD_REAL_VALUED, 6, 0);
  addfunc("gsl_sf_coupling_6j", amplgsl_sf_coupling_6j,
      FUNCADD_REAL_VALUED, 6, 0);
  addfunc("gsl_sf_coupling_9j", amplgsl_sf_coupling_9j,
      FUNCADD_REAL_VALUED, 9, 0);

  /* Dawson Function */
  addfunc("gsl_sf_dawson", amplgsl_sf_dawson, FUNCADD_REAL_VALUED, 1, 0);

  /* Debye Functions */
  addfunc("gsl_sf_debye_1", amplgsl_sf_debye_1, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_debye_2", amplgsl_sf_debye_2, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_debye_3", amplgsl_sf_debye_3, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_debye_4", amplgsl_sf_debye_4, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_debye_5", amplgsl_sf_debye_5, FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_debye_6", amplgsl_sf_debye_6, FUNCADD_REAL_VALUED, 1, 0);

  /* Dilogarithm */
  addfunc("gsl_sf_dilog", amplgsl_sf_dilog, FUNCADD_REAL_VALUED, 1, 0);

  /* Legendre Form of Complete Elliptic Integrals */
  addfunc("gsl_sf_ellint_Kcomp", amplgsl_sf_ellint_Kcomp,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_ellint_Ecomp", amplgsl_sf_ellint_Ecomp,
      FUNCADD_REAL_VALUED, 1, 0);
  addfunc("gsl_sf_ellint_Pcomp", amplgsl_sf_ellint_Pcomp,
      FUNCADD_REAL_VALUED, 2, 0);

  /* Legendre Form of Incomplete Elliptic Integrals */
  addfunc("gsl_sf_ellint_F", amplgsl_sf_ellint_F,
      FUNCADD_REAL_VALUED, 2, 0);
  // TODO: gsl_sf_ellint_E, gsl_sf_ellint_P, gsl_sf_ellint_D

  /* Carlson Forms */
  // TODO: gsl_sf_ellint_RC, gsl_sf_ellint_RD, gsl_sf_ellint_RF, gsl_sf_ellint_RJ

  /* Elliptic Functions (Jacobi) */
  // TODO

  /* Error Functions */
  // TODO

  /* Complementary Error Function */
  // TODO

  /* Log Complementary Error Function */
  // TODO
}
