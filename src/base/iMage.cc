// Copyright (C) 2004-2011, 2013, 2014, 2015 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <petscsys.h>

#include "base/stressbalance/PISMStressBalance.hh"
#include "base/util/IceGrid.hh"
#include "base/util/error_handling.hh"
#include "base/util/iceModelVec.hh"
#include "base/util/pism_options.hh"
#include "columnSystem.hh"
#include "iceModel.hh"

namespace pism {

//! Tridiagonal linear system for vertical column of age (pure advection) problem.
class ageSystemCtx : public columnSystemCtx {
public:
  ageSystemCtx(const std::vector<double>& storage_grid,
               const std::string &my_prefix,
               double dx, double dy, double dt,
               const IceModelVec3 &age,
               const IceModelVec3 &u3,
               const IceModelVec3 &v3,
               const IceModelVec3 &w3);

  void initThisColumn(int i, int j, double thickness);

  void solveThisColumn(std::vector<double> &x);
protected:
  const IceModelVec3 &m_age3;
  double m_nu;
  std::vector<double> m_A, m_A_n, m_A_e, m_A_s, m_A_w;
};


ageSystemCtx::ageSystemCtx(const std::vector<double>& storage_grid,
                           const std::string &my_prefix,
                           double dx, double dy, double dt,
                           const IceModelVec3 &age,
                           const IceModelVec3 &u3,
                           const IceModelVec3 &v3,
                           const IceModelVec3 &w3)
  : columnSystemCtx(storage_grid, my_prefix, dx, dy, dt, u3, v3, w3),
    m_age3(age) {

  size_t Mz = m_z.size();
  m_A.resize(Mz);
  m_A_n.resize(Mz);
  m_A_e.resize(Mz);
  m_A_s.resize(Mz);
  m_A_w.resize(Mz);

  m_nu = m_dt / m_dz; // derived constant
}

void ageSystemCtx::initThisColumn(int i, int j, double thickness) {
  init_column(i, j, thickness);

  if (m_ks == 0) {
    return;
  }

  coarse_to_fine(m_u3, i, j, &m_u[0]);
  coarse_to_fine(m_v3, i, j, &m_v[0]);
  coarse_to_fine(m_w3, i, j, &m_w[0]);

  coarse_to_fine(m_age3, m_i, m_j,   &m_A[0]);
  coarse_to_fine(m_age3, m_i, m_j+1, &m_A_n[0]);
  coarse_to_fine(m_age3, m_i+1, m_j, &m_A_e[0]);
  coarse_to_fine(m_age3, m_i, m_j-1, &m_A_s[0]);
  coarse_to_fine(m_age3, m_i-1, m_j, &m_A_w[0]);
}

//! Conservative first-order upwind scheme with implicit in the vertical: one column solve.
/*!
The PDE being solved is
    \f[ \frac{\partial \tau}{\partial t} + \frac{\partial}{\partial x}\left(u \tau\right) + \frac{\partial}{\partial y}\left(v \tau\right) + \frac{\partial}{\partial z}\left(w \tau\right) = 1. \f]
This PDE has the conservative form identified in the comments on IceModel::ageStep().

Let
    \f[ \mathcal{U}(x,y_{i+1/2}) = x \, \begin{Bmatrix} y_i, \quad x \ge 0 \\ y_{i+1}, \quad x \le 0 \end{Bmatrix}. \f]
Note that the two cases agree when \f$x=0\f$, so there is no conflict.  This is
part of the upwind rule, and \f$x\f$ will be the cell-boundary (finite volume sense)
value of the velocity.  Our discretization of the PDE uses this upwind notation
to build an explicit scheme for the horizontal terms and an implicit scheme for
the vertical terms, as follows.

Let
    \f[ A_{i,j,k}^n \approx \tau(x_i,y_j,z_k) \f]
be the numerical approximation of the exact value on the grid.  The scheme is
\f{align*}{
  \frac{A_{ijk}^{n+1} - A_{ijk}^n}{\Delta t} &+ \frac{\mathcal{U}(u_{i+1/2},A_{i+1/2,j,k}^n) - \mathcal{U}(u_{i-1/2},A_{i-1/2,j,k}^n)}{\Delta x} + \frac{\mathcal{U}(v_{j+1/2},A_{i,j+1/2,k}^n) - \mathcal{U}(v_{j-1/2},A_{i,j-1/2,k}^n)}{\Delta y} \\
    &\qquad \qquad + \frac{\mathcal{U}(w_{k+1/2},A_{i,j,k+1/2}^{n+1}) - \mathcal{U}(w_{k-1/2},A_{i,j,k-1/2}^{n+1})}{\Delta z} = 1.
  \f}
Here velocity components \f$u,v,w\f$ are all evaluated at time \f$t_n\f$, so
\f$u_{i+1/2} = u_{i+1/2,j,k}^n\f$ in more detail, and so on for all the other
velocity values.  Note that this discrete form
is manifestly conservative, in that, for example, the same term at \f$u_{i+1/2}\f$
is used both in updating \f$A_{i,j,k}^{n+1}\f$ and \f$A_{i+1,j,k}^{n+1}\f$.

Rewritten as a system of equations in the vertical index, let
   \f[ \Phi_k = \Delta t - \frac{\Delta t}{\Delta x} \left[\mathcal{U}(u_{i+1/2},A_{i+1/2,j,k}^n) - \mathcal{U}(u_{i-1/2},A_{i-1/2,j,k}^n)\right] - \frac{\Delta t}{\Delta y} \left[\mathcal{U}(v_{j+1/2},A_{i,j+1/2,k}^n) - \mathcal{U}(v_{j-1/2},A_{i,j-1/2,k}^n)\right]. \f]
Let \f$\nu = \Delta t / \Delta z\f$ and for slight simplification denote \f$w_\pm = w_{k\pm 1/2}\f$.  The equation determining the unknown
new age values in the column, denoted \f$a_k = A_{i,j,k}^{n+1}\f$, is
   \f[ a_k + \nu \left[\mathcal{U}(w_+,a_{k+1/2}) - \mathcal{U}(w_-,a_{k-1/2})\right] = A_{ijk}^n + \Phi_k. \f]
This is perhaps easiest to understand as four cases:
\f{align*}{
w_+\ge 0, w_-\ge 0:  && (-\nu w_-) a_{k-1} + (1 + \nu w_+) a_k + (0) a_{k+1} &= A_{ijk}^n + \Phi_k, \\
w_+\ge 0, w_- < 0:   && (0) a_{k-1} + (1 + \nu w_+ - \nu w_-) a_k + (0) a_{k+1} &= A_{ijk}^n + \Phi_k, \\
w_+ < 0,  w_-\ge 0:  && (-\nu w_-) a_{k-1} + (1) a_k + (+\nu w_+) a_{k+1} &= A_{ijk}^n + \Phi_k, \\
w_+ < 0,  w_- < 0:   && (0) a_{k-1} + (1 - \nu w_-) a_k + (+\nu w_+) a_{k+1} &= A_{ijk}^n + \Phi_k.
 \f}
These equation form a tridiagonal system, i.e. the bandwidth does not exceed three.
In every case the on-diagonal coefficient is greater than or equal to one,
while the off-diagonal coefficients are always negative.  The coefficients
approximately sum to one in each case, but only up to errors of size \f$O(\Delta z)\f$.
These facts APPARENTLY imply that the method has a maximum principle \ref MortonMayers.


FIXME:  THE COMMENT ABOVE HAS BEEN UPDATED TO THE 'CONSERVATIVE' FORM, BUT THE
CODE STILL REFLECTS THE OLD SCHEME.

FIXME:  CARE MUST BE TAKEN TO MAINTAIN CONSERVATISM AT SURFACE.
 */
void ageSystemCtx::solveThisColumn(std::vector<double> &x) {

  TridiagonalSystem &S = *m_solver;

  // set up system: 0 <= k < m_ks
  for (unsigned int k = 0; k < m_ks; k++) {
    // do lowest-order upwinding, explicitly for horizontal
    S.RHS(k) =  (m_u[k] < 0 ?
                 m_u[k] * (m_A_e[k] -  m_A[k]) / m_dx :
                 m_u[k] * (m_A[k]  - m_A_w[k]) / m_dx);
    S.RHS(k) += (m_v[k] < 0 ?
                 m_v[k] * (m_A_n[k] -  m_A[k]) / m_dy :
                 m_v[k] * (m_A[k]  - m_A_s[k]) / m_dy);
    // note it is the age eqn: dage/dt = 1.0 and we have moved the hor.
    //   advection terms over to right:
    S.RHS(k) = m_A[k] + m_dt * (1.0 - S.RHS(k));

    // do lowest-order upwinding, *implicitly* for vertical
    double AA = m_nu * m_w[k];
    if (k > 0) {
      if (AA >= 0) { // upward velocity
        S.L(k) = - AA;
        S.D(k) = 1.0 + AA;
        S.U(k) = 0.0;
      } else { // downward velocity; note  -AA >= 0
        S.L(k) = 0.0;
        S.D(k) = 1.0 - AA;
        S.U(k) = + AA;
      }
    } else { // k == 0 case
      // note L[0] is not used
      if (AA > 0) { // if strictly upward velocity apply boundary condition:
                    // age = 0 because ice is being added to base
        S.D(0) = 1.0;
        S.U(0) = 0.0;
        S.RHS(0) = 0.0;
      } else { // downward velocity; note  -AA >= 0
        S.D(0) = 1.0 - AA;
        S.U(0) = + AA;
        // keep rhs[0] as is
      }
    }
  }  // done "set up system: 0 <= k < m_ks"

  // surface b.c. at m_ks
  if (m_ks > 0) {
    S.L(m_ks) = 0;
    S.D(m_ks) = 1.0;   // ignore U[m_ks]
    S.RHS(m_ks) = 0.0;  // age zero at surface
  }

  // solve it
  try {
    S.solve(m_ks + 1, x);
  }
  catch (RuntimeError &e) {
    e.add_context("solving the tri-diagonal system (ageSystemCtx) at (%d, %d)\n"
                  "saving system to m-file... ", m_i, m_j);
    reportColumnZeroPivotErrorMFile(m_ks + 1);
    throw;
  }

  // x[k] contains age for k=0,...,ks, but set age of ice above (and
  // at) surface to zero years
  for (unsigned int k = m_ks + 1; k < x.size(); k++) {
    x[k] = 0.0;
  }
}


//! Take a semi-implicit time-step for the age equation.
/*!
Let \f$\tau(t,x,y,z)\f$ be the age of the ice.  Denote the three-dimensional
velocity field within the ice fluid as \f$(u,v,w)\f$.  The age equation
is \f$d\tau/dt = 1\f$, that is, ice may move but it gets one year older in one
year.  Thus
    \f[ \frac{\partial \tau}{\partial t} + u \frac{\partial \tau}{\partial x}
        + v \frac{\partial \tau}{\partial y} + w \frac{\partial \tau}{\partial z} = 1 \f]
This equation is purely advective and hyperbolic.  The right-hand side is "1" as
long as age \f$\tau\f$ and time \f$t\f$ are measured in the same units.
Because the velocity field is incompressible, \f$\nabla \cdot (u,v,w) = 0\f$,
we can rewrite the equation as
    \f[ \frac{\partial \tau}{\partial t} + \nabla \left( (u,v,w) \tau \right) = 1 \f]
There is a conservative first-order numerical method; see ageSystemCtx::solveThisColumn().

The boundary condition is that when the ice falls as snow it has age zero.
That is, \f$\tau(t,x,y,h(t,x,y)) = 0\f$ in accumulation areas.  There is no
boundary condition elsewhere on the ice upper surface, as the characteristics
go outward in the ablation zone.  If the velocity in the bottom cell of ice
is upward (\f$w>0\f$) then we also apply a zero age boundary condition,
\f$\tau(t,x,y,0) = 0\f$.  This is the case where ice freezes on at the base,
either grounded basal ice freezing on stored water in till, or marine basal ice.
(Note that the water that is frozen-on as ice might be quite "old" in the sense
that its most recent time in the atmosphere was long ago; this comment is
relevant to any analysis which relates isotope ratios to modeled age.)

The numerical method is a conservative form of first-order upwinding, but the
vertical advection term is computed implicitly.  Thus there is no CFL-type
stability condition from the vertical velocity; CFL is only for the horizontal
velocity.  We use a finely-spaced, equally-spaced vertical grid in the
calculation.  Note that the columnSystemCtx methods coarse_to_fine() and
fine_to_coarse() interpolate back and forth between this fine grid and
the storage grid.  The storage grid may or may not be equally-spaced.  See
ageSystemCtx::solveThisColumn() for the actual method.
 */
void IceModel::ageStep() {
  PetscErrorCode  ierr;

  bool viewOneColumn = options::Bool("-view_sys",
                                     "save column system information to file");

  const IceModelVec3
    &u3 = stress_balance->velocity_u(),
    &v3 = stress_balance->velocity_v(),
    &w3 = stress_balance->velocity_w();

  ageSystemCtx system(m_grid->z(), "age",
                      m_grid->dx(), m_grid->dy(), dt_TempAge,
                      age3, u3, v3, w3); // linear system to solve in each column

  size_t Mz_fine = system.z().size();
  std::vector<double> x(Mz_fine);   // space for solution

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(age3);
  list.add(u3);
  list.add(v3);
  list.add(w3);
  list.add(vWork3d);

  ParallelSection loop(m_grid->com);
  try {
    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      system.initThisColumn(i, j, ice_thickness(i, j));

      if (system.ks() == 0) {
        // if no ice, set the entire column to zero age
        vWork3d.set_column(i, j, 0.0);
      } else {
        // general case: solve advection PDE

        // solve the system for this column; call checks that params set
        system.solveThisColumn(x);

        if (viewOneColumn && (i == id && j == jd)) {
          ierr = PetscPrintf(PETSC_COMM_SELF,
                             "\n"
                             "in ageStep(): saving ageSystemCtx at (i,j)=(%d,%d) to m-file... \n",
                             i, j);
          PISM_CHK(ierr, "PetscPrintf");

          system.viewColumnInfoMFile(x);
        }

        // put solution in IceModelVec3
        system.fine_to_coarse(x, i, j, vWork3d);

        // Ensure that the age of the ice is non-negative.
        //
        // FIXME: this is a kludge. We need to ensure that our numerical method has the maximum
        // principle instead. (We may still need this for correctness, though.)
        double *column = vWork3d.get_column(i, j);
        for (unsigned int k = 0; k < m_grid->Mz(); ++k) {
          if (column[k] < 0.0) {
            column[k] = 0.0;
          }
        }
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  vWork3d.update_ghosts(age3);
}


} // end of namespace pism
