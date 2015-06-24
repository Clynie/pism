// Copyright (C) 2011, 2012, 2013, 2014, 2015 The PISM Authors
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


#include "base/util/pism_const.hh"
#include "varcEnthalpyConverter.hh"

#include "base/util/error_handling.hh"

namespace pism {

varcEnthalpyConverter::varcEnthalpyConverter(const Config &config)
  : EnthalpyConverter(config), m_T_r(256.81786846822), m_c_gradient(7.253) {
  // empty
}

varcEnthalpyConverter::~varcEnthalpyConverter() {
  // empty
}

/*!
A calculation only used in the cold case.

Let \f$c_i=2009.0\,\, \text{J}\,\text{kg}^{-1}\,\text{K}^{-1}\f$, the default
constant value.  Note equation (4.39) in [\ref GreveBlatter2009] says 
\f$C(T) = c_i + 7.253 (T - T_r)\f$ using a reference temperature
\f$T_r = 256.81786846822\f$ K.  Thus the calculation of enthalpy from cold ice
temperature is: 
\f{align*}{
  E(T) &= \int_{T_0}^T C(T')\,dT \\
       &= c_i (T-T_0) + \frac{7.253}{2} \left((T-T_r)^2 - (T_0-T_r)^2\right) \\
       &= \left(c_i + 7.253 \left(\frac{T+T_0}{2} - T_r\right)\right) (T- T_0).
\f}
 */
double varcEnthalpyConverter::EfromT(double T) const {
  const double
     Trefdiff = 0.5 * (T + m_T_0) - m_T_r; 
  return (m_c_i + m_c_gradient * Trefdiff) * (T - m_T_0);
}


/*!
A calculation only used in the cold case.

From the documentation for EfromT(), in the cold ice case we must solve the equation
  \f[ E = \left(c_i + 7.253 \left(\frac{T+T_0}{2} - T_r\right)\right) (T- T_0) \f]
for \f$T\f$.  This equation is quadratic in \f$T\f$.  If we write it in terms
of \f$\Delta T = T-T_0\f$ and \f$\alpha = 2/7.253\f$ then it says
  \f[ E = c_i \Delta T + \alpha^{-1} \left(\Delta T + 2(T_0-T_r)\right) \Delta T.\f]
Rearranging as a standard form quadratic in unknown \f$\Delta T\f$ gives
  \f[ 0 = \Delta T^2 + \left[\alpha c_i + 2(T_0-T_r)\right] \Delta T - \alpha E.\f]
Define
  \f[ \beta = \alpha c_i + 2(T_0-T_r) = 486.64, \f]
which has units K; the value comes from \f$c_i=2009\f$, \f$T_0=223.15\f$, and
\f$T_r=256.81786846822\f$.  Then the solution of the quadratic is the one which makes 
\f$\Delta T \ge 0\f$ assuming \f$E\ge 0\f$; we stop otherwise.  With the usual
rewriting to avoid cancellation we have
  \f[ \Delta T = \frac{-\beta + \sqrt{\beta^2 + 4 \alpha E}}{2} = \frac{2 \alpha E}{\sqrt{\beta^2 + 4 \alpha E} + \beta}.\f]
Of course, \f$T=T_0 + \Delta T\f$.
 */
double varcEnthalpyConverter::TfromE(double E) const {
  if (E < 0.0) {
    throw RuntimeError("E < 0 in varcEnthalpyConverter is not allowed.");
  }
  const double
    ALPHA = 2.0 / m_c_gradient,
    BETA  = ALPHA * m_c_i + 2.0 * (m_T_0 - m_T_r),
    tmp   = 2.0 * ALPHA * E,
    dT    = tmp / (sqrt(BETA*BETA + 2.0*tmp) + BETA);
  return m_T_0 + dT;
}

//! Redefined from EnthalpyConverter version, for use when specific heat capacity depends on temperature.
/*!
Calls EfromT().
 */
double varcEnthalpyConverter::enthalpy_cts_impl(double p) const {
  return EfromT(melting_temperature(p));
}

/*!
  Equation (4.39) in [\ref GreveBlatter2009] is
  \f$C(T) = c_i + 7.253 (T - T_r)\f$, with a reference temperature
  \f$T_r = 256.82\f$ K.
*/
double varcEnthalpyConverter::c_from_T_impl(double T) const {
  return m_c_i + m_c_gradient * (T - m_T_r);
}

//! Redefined from EnthalpyConverter version, for use when specific heat capacity depends on temperature.
/*!
Calls TfromE().
 */
double varcEnthalpyConverter::temperature_impl(double E, double p) const {

#if (PISM_DEBUG==1)
  if (E >= enthalpy_liquid(p)) {
    throw RuntimeError::formatted("E=%f at p=%f equals or exceeds that of liquid water",
                                  E, p);
  }
#endif

  if (E < enthalpy_cts(p)) {
    return TfromE(E);
  } else {
    return melting_temperature(p);
  }
}


//! Redefined from EnthalpyConverter version, for use when specific heat capacity depends on temperature.
/*!
Calls EfromT().
 */
double varcEnthalpyConverter::enthalpy_impl(double T, double omega, double p) const {
  const double T_m = melting_temperature(p);

  if (T <= 0.0) {
    throw RuntimeError::formatted("T = %f <= 0 is not a valid absolute temperature",T);
  }
  if ((omega < 0.0 - 1.0e-6) || (1.0 + 1.0e-6 < omega)) {
    throw RuntimeError::formatted("water fraction omega=%f not in range [0,1]",omega);
  }
  if (T > T_m + 1.0e-6) {
    throw RuntimeError::formatted("T=%f exceeds T_m=%f; not allowed",T,T_m);
  }
  if ((T < T_m - 1.0e-6) && (omega > 0.0 + 1.0e-6)) {
    throw RuntimeError::formatted("T < T_m AND omega > 0 is contradictory; got T=%f, T_m=%f, omega=%f",
                                  T, T_m, omega);
  }

  if (T < T_m) {
    return EfromT(T);
  } else {
    return enthalpy_cts(p) + omega * m_L;
  }
}


} // end of namespace pism
