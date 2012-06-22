// AMPL bindings for the GNU Scientific Library.

#include <math.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_sf_bessel.h>
#include "solvers/funcadd.h"

static real amplgsl_log1p(arglist *al) {
  real x = al->ra[0];
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

// TODO: gsl_hypot3, gsl_ldexp

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

void funcadd_ASL(AmplExports *ae) {
  addfunc("gsl_log1p", amplgsl_log1p, 0, 1, 0);
  addfunc("gsl_expm1", amplgsl_expm1, 0, 1, 0);
  addfunc("gsl_hypot", amplgsl_hypot, 0, 2, 0);
  addfunc("gsl_sf_bessel_J0", amplgsl_sf_bessel_J0, 0, 1, 0);
}
