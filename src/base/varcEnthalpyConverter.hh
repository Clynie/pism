// Copyright (C) 2011, 2014, 2015 Ed Bueler
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

#ifndef __varcEnthalpyConverter_hh
#define __varcEnthalpyConverter_hh

#include "enthalpyConverter.hh"

namespace pism {

//! Enthalpy converter based on specific heat which is linear in temperature.
/*!
  This EnthalpyConverter object converts between enthalpy and temperature using
  linear-in-temperature specific heat capacity.  We implement only 
  Equation (4.39) in [\ref GreveBlatter2009],
  \f[ C(T) = 146.3 + 7.253 T = c_i + 7.253 (T - T_r) \f]
  where \f$T\f$ is in Kelvin, \f$c_i = 2009\,\, \text{J}\,\text{kg}^{-1}\,\text{K}^{-1}\f$,
  and the reference temperature is \f$T_r = 256.81786846822\f$ K.
*/
class varcEnthalpyConverter : public EnthalpyConverter {
public:
  varcEnthalpyConverter(const Config &config);
  virtual ~varcEnthalpyConverter();

protected:
  double enthalpy_cts_impl(double p) const;
  double c_from_T_impl(double T) const;
  double enthalpy_impl(double T, double omega, double p) const;
  double temperature_impl(double E, double p) const;

  //!< reference temperature in the parameterization of C(T)
  const double m_T_r;
  //!< \brief the rate of change of C with respect to T in the
  //! parameterization of C(T)
  const double m_c_gradient;

  double EfromT(double T) const;
  double TfromE(double E) const;
};

} // end of namespace pism

#endif // __varcEnthalpyConverter_hh

