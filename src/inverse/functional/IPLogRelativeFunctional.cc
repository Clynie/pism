// Copyright (C) 2012, 2014  David Maxwell
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

#include "IPLogRelativeFunctional.hh"

namespace pism {

//! Determine the normalization constant for the functional.
/*! Sets the normalization constant \f$c_N\f$ so that
\f[
J(x)=1
\f]
if \f$|x| = \mathtt{scale}\f$ everywhere.
*/
PetscErrorCode IPLogRelativeFunctional::normalize(double scale) {
  PetscErrorCode   ierr;

  // The local value of the weights
  double value = 0;

  double scale_sq = scale*scale;

  double w = 1.;

  IceModelVec::AccessList list;
  list.add(m_u_observed);

  if (m_weights) {
    list.add(*m_weights);
  }

  for (Points p(m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    Vector2 &u_obs_ij = m_u_observed(i, j);
    if (m_weights) {
      w = (*m_weights)(i, j);
    }
    double obsMagSq = (u_obs_ij.u*u_obs_ij.u + u_obs_ij.v*u_obs_ij.v) + m_eps*m_eps;
    value += log(1 + w*scale_sq/obsMagSq);
  }

  ierr = GlobalSum(m_grid.com, &value,  &m_normalization); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode IPLogRelativeFunctional::valueAt(IceModelVec2V &x, double *OUTPUT)  {
  PetscErrorCode   ierr;

  // The value of the objective
  double value = 0;

  double w = 1;

  IceModelVec::AccessList list;
  list.add(x);
  list.add(m_u_observed);
  if (m_weights) {
    list.add(*m_weights);
  }

  for (Points p(m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    Vector2 &x_ij = x(i, j);
    Vector2 &u_obs_ij = m_u_observed(i, j);
    if (m_weights) {
      w = (*m_weights)(i, j);
    }
    double obsMagSq = (u_obs_ij.u*u_obs_ij.u + u_obs_ij.v*u_obs_ij.v) + m_eps*m_eps;
    value += log(1 + w*(x_ij.u*x_ij.u + x_ij.v*x_ij.v)/obsMagSq);
  }

  value /= m_normalization;

  ierr = GlobalSum(m_grid.com, &value,  OUTPUT); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IPLogRelativeFunctional::gradientAt(IceModelVec2V &x, IceModelVec2V &gradient)  {
  gradient.set(0);

  double w = 1;

  IceModelVec::AccessList list;
  list.add(x);
  list.add(gradient);
  list.add(m_u_observed);
  if (m_weights) {
    list.add(*m_weights);
  }

  for (Points p(m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    Vector2 &x_ij = x(i, j);
    Vector2 &u_obs_ij = m_u_observed(i, j);
    if (m_weights) {
      w = (*m_weights)(i, j);
    }
    double obsMagSq = u_obs_ij.u*u_obs_ij.u + u_obs_ij.v*u_obs_ij.v + m_eps*m_eps;
    double dJdxsq =  w/(obsMagSq + w*(x_ij.u*x_ij.u + x_ij.v*x_ij.v));

    gradient(i, j).u = dJdxsq*2*x_ij.u/m_normalization;
    gradient(i, j).v = dJdxsq*2*x_ij.v/m_normalization;
  }

  return 0;
}

} // end of namespace pism
