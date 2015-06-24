// Copyright (C) 2013, 2014, 2015  David Maxwell and Constantine Khroulev
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

#include <cassert>

#include "IP_SSAHardavForwardProblem.hh"
#include "base/basalstrength/basal_resistance.hh"
#include "base/rheology/flowlaws.hh"
#include "base/util/IceGrid.hh"
#include "base/util/Mask.hh"
#include "base/util/PISMConfigInterface.hh"
#include "base/util/PISMVars.hh"
#include "base/util/error_handling.hh"
#include "base/util/pism_const.hh"

namespace pism {
namespace inverse {

IP_SSAHardavForwardProblem::IP_SSAHardavForwardProblem(IceGrid::ConstPtr g, EnthalpyConverter::Ptr e,
                                                       IPDesignVariableParameterization &tp)
  : SSAFEM(g, e),
    m_zeta(NULL),
    m_fixed_design_locations(NULL),
    m_design_param(tp),
    m_element_index(*m_grid),
    m_quadrature(*m_grid, 1.0),
    m_rebuild_J_state(true) {
  this->construct();
}

IP_SSAHardavForwardProblem::~IP_SSAHardavForwardProblem() {
  // empty
}

void IP_SSAHardavForwardProblem::construct() {
  PetscErrorCode ierr;
  int stencilWidth = 1;

  m_velocity_shared.reset(new IceModelVec2V);
  m_velocity_shared->create(m_grid, "dummy", WITHOUT_GHOSTS);
  m_velocity_shared->metadata(0) = m_velocity.metadata(0);
  m_velocity_shared->metadata(1) = m_velocity.metadata(1);

  m_dzeta_local.create(m_grid, "d_zeta_local", WITH_GHOSTS, stencilWidth);
  m_hardav.create(m_grid, "hardav", WITH_GHOSTS, stencilWidth);

  m_du_global.create(m_grid, "linearization work vector (sans ghosts)",
                     WITHOUT_GHOSTS, stencilWidth);
  m_du_local.create(m_grid, "linearization work vector (with ghosts)",
                    WITH_GHOSTS, stencilWidth);

#if PETSC_VERSION_LT(3,5,0)
  ierr = DMCreateMatrix(*m_da, "baij", m_J_state.rawptr());
  PISM_CHK(ierr, "DMCreateMatrix");
#else
  ierr = DMSetMatType(*m_da, MATBAIJ);
  PISM_CHK(ierr, "DMSetMatType");

  ierr = DMCreateMatrix(*m_da, m_J_state.rawptr());
  PISM_CHK(ierr, "DMCreateMatrix");
#endif

  ierr = KSPCreate(m_grid->com, m_ksp.rawptr());
  PISM_CHK(ierr, "KSPCreate");

  double ksp_rtol = 1e-12;
  ierr = KSPSetTolerances(m_ksp, ksp_rtol, PETSC_DEFAULT, PETSC_DEFAULT, PETSC_DEFAULT);
  PISM_CHK(ierr, "KSPSetTolerances");

  PC pc;
  ierr = KSPGetPC(m_ksp, &pc);
  PISM_CHK(ierr, "KSPGetPC");

  ierr = PCSetType(pc, PCBJACOBI);
  PISM_CHK(ierr, "PCSetType");

  ierr = KSPSetFromOptions(m_ksp);
  PISM_CHK(ierr, "KSPSetFromOptions");
}

//! Sets the current value of of the design paramter \f$\zeta\f$.
/*! This method sets \f$\zeta\f$ but does not solve the %SSA.
It it intended for inverse methods that simultaneously compute
the pair \f$u\f$ and \f$\zeta\f$ without ever solving the %SSA
directly.  Use this method in conjuction with
\ref assemble_jacobian_state and \ref apply_jacobian_design and their friends.
The vector \f$\zeta\f$ is not copied; a reference to the IceModelVec is
kept.
*/
void IP_SSAHardavForwardProblem::set_design(IceModelVec2S &new_zeta) {

  using fem::Quadrature;

  m_zeta = &new_zeta;

  // Convert zeta to hardav.
  m_design_param.convertToDesignVariable(*m_zeta, m_hardav);

  // Cache hardav at the quadrature points in m_coefficients.
  double hardav_q[Quadrature::Nq];
  IceModelVec::AccessList list(m_hardav);

  int xs = m_element_index.xs, xm = m_element_index.xm,
    ys = m_element_index.ys, ym = m_element_index.ym;
  for (int i = xs; i < xs + xm; i++) {
    for (int j = ys; j < ys + ym; j++) {
      m_quadrature.computeTrialFunctionValues(i, j, m_dofmap, m_hardav, hardav_q);
      const int ij = m_element_index.flatten(i, j);
      Coefficients *coefficients = &m_coefficients[ij*Quadrature::Nq];
      for (unsigned int q = 0; q < Quadrature::Nq; q++) {
        coefficients[q].B = hardav_q[q];
      }
    }
  }

  // Flag the state jacobian as needing rebuilding.
  m_rebuild_J_state = true;
}

//! Sets the current value of the design variable \f$\zeta\f$ and solves the %SSA to find the associated \f$u_{\rm SSA}\f$.
/* Use this method for inverse methods employing the reduced gradient. Use this method
in conjuction with apply_linearization and apply_linearization_transpose.*/
TerminationReason::Ptr IP_SSAHardavForwardProblem::linearize_at(IceModelVec2S &zeta) {
  this->set_design(zeta);
  return this->solve_nocache();
}

//! Computes the residual function \f$\mathcal{R}(u, \zeta)\f$ as defined in the class-level documentation.
/* The value of \f$\zeta\f$ is set prior to this call via set_design or linearize_at. The value
of the residual is returned in \a RHS.*/
void IP_SSAHardavForwardProblem::assemble_residual(IceModelVec2V &u, IceModelVec2V &RHS) {

  Vector2
    **u_a   = u.get_array(),
    **rhs_a = RHS.get_array();

  DMDALocalInfo *info = NULL;
  this->compute_local_function(info, const_cast<const Vector2 **>(u_a), rhs_a);

  u.end_access();
  RHS.end_access();
}

//! Computes the residual function \f$\mathcal{R}(u, \zeta)\f$ defined in the class-level documentation.
/* The return value is specified via a Vec for the benefit of certain TAO routines.  Otherwise,
the method is identical to the assemble_residual returning values as a StateVec (an IceModelVec2V).*/
void IP_SSAHardavForwardProblem::assemble_residual(IceModelVec2V &u, Vec RHS) {

  Vector2 **u_a = u.get_array();

  petsc::DMDAVecArray rhs_a(m_da, RHS);
  DMDALocalInfo *info = NULL;
  this->compute_local_function(info, const_cast<const Vector2 **>(u_a),
                               (Vector2**)rhs_a.get());
  u.end_access();
}

//! Assembles the state Jacobian matrix.
/* The matrix depends on the current value of the design variable \f$\zeta\f$ and the current
value of the state variable \f$u\f$.  The specification of \f$\zeta\f$ is done earlier
with set_design or linearize_at.  The value of \f$u\f$ is specified explicitly as an argument
to this method.
  @param[in] u Current state variable value.
  @param[out] J computed state Jacobian.
*/
void IP_SSAHardavForwardProblem::assemble_jacobian_state(IceModelVec2V &u, Mat Jac) {

  Vector2 **u_a = u.get_array();

  DMDALocalInfo *info = NULL;
  this->compute_local_jacobian(info, const_cast<const Vector2 **>(u_a), Jac);

  u.end_access();
}

//! Applies the design Jacobian matrix to a perturbation of the design variable.
/*! The return value uses a DesignVector (IceModelVec2V), which can be ghostless. Ghosts (if present) are updated.
\overload
*/
void IP_SSAHardavForwardProblem::apply_jacobian_design(IceModelVec2V &u,
                                                       IceModelVec2S &dzeta,
                                                       IceModelVec2V &du) {
  Vector2 **du_a = du.get_array();
  this->apply_jacobian_design(u, dzeta, du_a);
  du.end_access();
}

//! Applies the design Jacobian matrix to a perturbation of the design variable.
/*! The return value is a Vec for the benefit of TAO. It is assumed to be ghostless; no communication is done.
\overload
*/
void IP_SSAHardavForwardProblem::apply_jacobian_design(IceModelVec2V &u,
                                                       IceModelVec2S &dzeta,
                                                       Vec du) {
  petsc::DMDAVecArray du_a(m_da, du);
  this->apply_jacobian_design(u, dzeta, (Vector2**)du_a.get());
}

//! @brief Applies the design Jacobian matrix to a perturbation of the
//! design variable.

/*! The matrix depends on the current value of the design variable
    \f$\zeta\f$ and the current value of the state variable \f$u\f$.
    The specification of \f$\zeta\f$ is done earlier with set_design
    or linearize_at. The value of \f$u\f$ is specified explicitly as
    an argument to this method.

  @param[in] u Current state variable value.

  @param[in] dzeta Perturbation of the design variable. Prefers
                   vectors with ghosts; will copy to a ghosted vector
                   if needed.

  @param[out] du_a Computed corresponding perturbation of the state
                   variable. The array \a du_a should be extracted
                   first from a Vec or an IceModelVec.

  Typically this method is called via one of its overloads.
*/
void IP_SSAHardavForwardProblem::apply_jacobian_design(IceModelVec2V &u,
                                                       IceModelVec2S &dzeta,
                                                       Vector2 **du_a) {
  using fem::Quadrature;

  IceModelVec::AccessList list;
  list.add(*m_zeta);
  list.add(u);

  IceModelVec2S *dzeta_local;
  if (dzeta.get_stencil_width() > 0) {
    dzeta_local = &dzeta;
  } else {
    m_dzeta_local.copy_from(dzeta);
    dzeta_local = &m_dzeta_local;
  }
  list.add(*dzeta_local);

  // Zero out the portion of the function we are responsible for computing.
  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    du_a[i][j].u = 0.0;
    du_a[i][j].v = 0.0;
  }

  // Aliases to help with notation consistency below.
  const IceModelVec2Int *m_dirichletLocations = m_bc_mask;
  const IceModelVec2V   *m_dirichletValues    = m_bc_values;
  double           m_dirichletWeight    = m_dirichletScale;

  Vector2 u_e[Quadrature::Nk];
  Vector2 u_q[Quadrature::Nq];
  double Du_q[Quadrature::Nq][3];

  Vector2 du_e[Quadrature::Nk];

  double dzeta_e[Quadrature::Nk];

  double zeta_e[Quadrature::Nk];

  double dB_e[Quadrature::Nk];
  double dB_q[Quadrature::Nq];

  // An Nq by Nk array of test function values.
  const fem::FunctionGerm (*test)[Quadrature::Nk] = m_quadrature.testFunctionValues();

  fem::DirichletData_Vector dirichletBC;
  dirichletBC.init(m_dirichletLocations, m_dirichletValues,
                   m_dirichletWeight);
  fem::DirichletData_Scalar fixedZeta;
  fixedZeta.init(m_fixed_design_locations, NULL);

  // Jacobian times weights for quadrature.
  const double* JxW = m_quadrature.getWeightedJacobian();

  // Loop through all elements.
  int xs = m_element_index.xs,
    xm   = m_element_index.xm,
    ys   = m_element_index.ys,
    ym   = m_element_index.ym;

  ParallelSection loop(m_grid->com);
  try {
    for (int i =xs; i<xs+xm; i++) {
      for (int j =ys; j<ys+ym; j++) {

        // Zero out the element-local residual in prep for updating it.
        for (unsigned int k=0; k<Quadrature::Nk; k++) {
          du_e[k].u = 0;
          du_e[k].v = 0;
        }

        // Index into coefficient storage in m_coefficients
        const int ij = m_element_index.flatten(i, j);

        // Initialize the map from global to local degrees of freedom for this element.
        m_dofmap.reset(i, j, *m_grid);

        // Obtain the value of the solution at the nodes adjacent to the element,
        // fix dirichlet values, and compute values at quad pts.
        m_dofmap.extractLocalDOFs(i, j, u, u_e);
        if (dirichletBC) {
          dirichletBC.constrain(m_dofmap);
          dirichletBC.update(m_dofmap, u_e);
        }
        m_quadrature_vector.computeTrialFunctionValues(u_e, u_q, Du_q);

        // Compute dzeta at the nodes
        m_dofmap.extractLocalDOFs(i, j, *dzeta_local, dzeta_e);
        if (fixedZeta) {
          fixedZeta.update_homogeneous(m_dofmap, dzeta_e);
        }

        // Compute the change in hardav with respect to zeta at the quad points.
        m_dofmap.extractLocalDOFs(i, j, *m_zeta, zeta_e);
        for (unsigned int k=0; k<Quadrature::Nk; k++) {
          m_design_param.toDesignVariable(zeta_e[k], NULL, dB_e + k);
          dB_e[k]*=dzeta_e[k];
        }
        m_quadrature.computeTrialFunctionValues(dB_e, dB_q);

        for (unsigned int q = 0; q < Quadrature::Nq; q++) {
          // Symmetric gradient at the quadrature point.
          double *Duqq = Du_q[q];

          const Coefficients *coefficients = &m_coefficients[ij*Quadrature::Nq + q];

          double d_nuH = 0;
          if (coefficients->H >= strength_extension->get_min_thickness()) {
            m_flow_law->effective_viscosity(dB_q[q], secondInvariantDu_2D(Duqq), &d_nuH, NULL);
            d_nuH *= (2*coefficients->H);
          }

          for (unsigned int k = 0; k < Quadrature::Nk; k++) {
            const fem::FunctionGerm &testqk = test[q][k];
            du_e[k].u += JxW[q]*d_nuH*(testqk.dx*(2*Duqq[0] + Duqq[1]) + testqk.dy*Duqq[2]);
            du_e[k].v += JxW[q]*d_nuH*(testqk.dy*(2*Duqq[1] + Duqq[0]) + testqk.dx*Duqq[2]);
          }
        } // q
        m_dofmap.addLocalResidualBlock(du_e, du_a);
      } // j
    } // i
  } catch (...) {
    loop.failed();
  }
  loop.check();


  if (dirichletBC) {
    dirichletBC.fix_residual_homogeneous(du_a);
  }

  dirichletBC.finish();
  fixedZeta.finish();
}

//! Applies the transpose of the design Jacobian matrix to a perturbation of the state variable.
/*! The return value uses a StateVector (IceModelVec2S) which can be ghostless; ghosts (if present) are updated.
\overload
*/
void IP_SSAHardavForwardProblem::apply_jacobian_design_transpose(IceModelVec2V &u,
                                                                 IceModelVec2V &du,
                                                                 IceModelVec2S &dzeta) {
  double **dzeta_a = dzeta.get_array();
  this->apply_jacobian_design_transpose(u, du, dzeta_a);
  dzeta.end_access();
}

//! Applies the transpose of the design Jacobian matrix to a perturbation of the state variable.
/*! The return value uses a Vec for the benefit of TAO.  It is assumed to be ghostless; no communication is done.
\overload */
void IP_SSAHardavForwardProblem::apply_jacobian_design_transpose(IceModelVec2V &u,
                                                                 IceModelVec2V &du,
                                                                 Vec dzeta) {

  petsc::DM::Ptr da2 = m_grid->get_dm(1, m_config->get_double("grid_max_stencil_width"));
  petsc::DMDAVecArray dzeta_a(da2, dzeta);
  this->apply_jacobian_design_transpose(u, du, (double**)dzeta_a.get());
}

//! @brief Applies the transpose of the design Jacobian matrix to a
//! perturbation of the state variable.

/*! The matrix depends on the current value of the design variable
    \f$\zeta\f$ and the current value of the state variable \f$u\f$.
    The specification of \f$\zeta\f$ is done earlier with set_design
    or linearize_at. The value of \f$u\f$ is specified explicitly as
    an argument to this method.

  @param[in] u Current state variable value.

  @param[in] du Perturbation of the state variable. Prefers vectors
                with ghosts; will copy to a ghosted vector if need be.

  @param[out] dzeta_a Computed corresponding perturbation of the
                      design variable. The array \a dzeta_a should be
                      extracted first from a Vec or an IceModelVec.

  Typically this method is called via one of its overloads.
*/
void IP_SSAHardavForwardProblem::apply_jacobian_design_transpose(IceModelVec2V &u,
                                                                 IceModelVec2V &du,
                                                                 double **dzeta_a) {
  using fem::Quadrature;

  IceModelVec::AccessList list;
  list.add(*m_zeta);
  list.add(u);

  IceModelVec2V *du_local;
  if (du.get_stencil_width() > 0) {
    du_local = &du;
  } else {
    m_du_local.copy_from(du);
    du_local = &m_du_local;
  }
  list.add(*du_local);

  Vector2 u_e[Quadrature::Nk];
  Vector2 u_q[Quadrature::Nq];
  double Du_q[Quadrature::Nq][3];

  Vector2 du_e[Quadrature::Nk];
  Vector2 du_q[Quadrature::Nq];
  Vector2 du_dx_q[Quadrature::Nq];
  Vector2 du_dy_q[Quadrature::Nq];

  double dzeta_e[Quadrature::Nk];

  // An Nq by Nk array of test function values.
  const fem::FunctionGerm (*test)[Quadrature::Nk] = m_quadrature.testFunctionValues();

  fem::DirichletData_Vector dirichletBC;
  // Aliases to help with notation consistency.
  const IceModelVec2Int *m_dirichletLocations = m_bc_mask;
  const IceModelVec2V   *m_dirichletValues = m_bc_values;
  double        m_dirichletWeight = m_dirichletScale;
  dirichletBC.init(m_dirichletLocations, m_dirichletValues,
                   m_dirichletWeight);

  // Jacobian times weights for quadrature.
  const double* JxW = m_quadrature.getWeightedJacobian();

  // Zero out the portion of the function we are responsible for computing.
  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    dzeta_a[i][j] = 0;
  }

  int xs = m_element_index.xs, xm = m_element_index.xm,
           ys = m_element_index.ys, ym = m_element_index.ym;
  ParallelSection loop(m_grid->com);
  try {
    for (int i = xs; i < xs + xm; i++) {
      for (int j = ys; j < ys + ym; j++) {
        // Index into coefficient storage in m_coefficients
        const int ij = m_element_index.flatten(i, j);

        // Initialize the map from global to local degrees of freedom for this element.
        m_dofmap.reset(i, j, *m_grid);

        // Obtain the value of the solution at the nodes adjacent to the element.
        // Compute the solution values and symmetric gradient at the quadrature points.
        m_dofmap.extractLocalDOFs(i, j, du, du_e);
        if (dirichletBC) {
          dirichletBC.update_homogeneous(m_dofmap, du_e);
        }
        m_quadrature_vector.computeTrialFunctionValues(du_e, du_q, du_dx_q, du_dy_q);

        m_dofmap.extractLocalDOFs(i, j, u, u_e);
        if (dirichletBC) {
          dirichletBC.update(m_dofmap, u_e);
        }
        m_quadrature_vector.computeTrialFunctionValues(u_e, u_q, Du_q);

        // Zero out the element - local residual in prep for updating it.
        for (unsigned int k = 0; k < Quadrature::Nk; k++) {
          dzeta_e[k] = 0;
        }

        for (unsigned int q = 0; q < Quadrature::Nq; q++) {
          // Symmetric gradient at the quadrature point.
          double *Duqq = Du_q[q];

          const Coefficients *coefficients = &m_coefficients[ij*Quadrature::Nq + q];

          // Determine "d_nuH / dB" at the quadrature point
          double d_nuH_dB = 0;
          if (coefficients->H >= strength_extension->get_min_thickness()) {
            m_flow_law->effective_viscosity(1., secondInvariantDu_2D(Duqq), &d_nuH_dB, NULL);
            d_nuH_dB *= (2*coefficients->H);
          }

          for (unsigned int k = 0; k < Quadrature::Nk; k++) {
            dzeta_e[k] += JxW[q]*d_nuH_dB*test[q][k].val*((du_dx_q[q].u*(2*Duqq[0] + Duqq[1]) +
                                                           du_dy_q[q].u*Duqq[2]) +
                                                          (du_dy_q[q].v*(2*Duqq[1] + Duqq[0]) +
                                                           du_dx_q[q].v*Duqq[2]));
          }
        } // q

        m_dofmap.addLocalResidualBlock(dzeta_e, dzeta_a);
      } // j
    } // i
  } catch (...) {
    loop.failed();
  }
  loop.check();

  dirichletBC.finish();

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double dB_dzeta;
    m_design_param.toDesignVariable((*m_zeta)(i, j), NULL, &dB_dzeta);
    dzeta_a[i][j] *= dB_dzeta;
  }

  if (m_fixed_design_locations) {
    fem::DirichletData_Scalar fixedZeta;
    fixedZeta.init(m_fixed_design_locations, NULL);
    fixedZeta.fix_residual_homogeneous(dzeta_a);
    fixedZeta.finish();
  }
}

/*!\brief Applies the linearization of the forward map (i.e. the reduced gradient \f$DF\f$ described in
the class-level documentation.) */
/*! As described previously,
\f[
Df = J_{\rm State}^{-1} J_{\rm Design}.
\f]
Applying the linearization then involves the solution of a linear equation.
The matrices \f$J_{\rm State}\f$ and \f$J_{\rm Design}\f$ both depend on the value of the
design variable \f$\zeta\f$ and the value of the corresponding state variable \f$u=F(\zeta)\f$.
These are established by first calling linearize_at.
  @param[in]   dzeta     Perturbation of the design variable
  @param[out]  du        Computed corresponding perturbation of the state variable; ghosts (if present) are updated.
*/
void IP_SSAHardavForwardProblem::apply_linearization(IceModelVec2S &dzeta, IceModelVec2V &du) {

  PetscErrorCode ierr;

  if (m_rebuild_J_state) {
    this->assemble_jacobian_state(m_velocity, m_J_state);
    m_rebuild_J_state = false;
  }

  this->apply_jacobian_design(m_velocity, dzeta, m_du_global);
  m_du_global.scale(-1);

  // call PETSc to solve linear system by iterative method.
#if PETSC_VERSION_LT(3,5,0)
  ierr = KSPSetOperators(m_ksp, m_J_state, m_J_state, SAME_NONZERO_PATTERN);
  PISM_CHK(ierr, "KSPSetOperators");
#else
  ierr = KSPSetOperators(m_ksp, m_J_state, m_J_state);
  PISM_CHK(ierr, "KSPSetOperators");
#endif
  ierr = KSPSolve(m_ksp, m_du_global.get_vec(), m_du_global.get_vec());
  PISM_CHK(ierr, "KSPSolve"); // SOLVE

  KSPConvergedReason reason;
  ierr = KSPGetConvergedReason(m_ksp, &reason);
  PISM_CHK(ierr, "KSPGetConvergedReason");

  if (reason < 0) {
    throw RuntimeError::formatted("IP_SSAHardavForwardProblem::apply_linearization solve"
                                  " failed to converge (KSP reason %s)",
                                  KSPConvergedReasons[reason]);
  } else {
    verbPrintf(4, m_grid->com,
               "IP_SSAHardavForwardProblem::apply_linearization converged"
               " (KSP reason %s)\n",
               KSPConvergedReasons[reason]);
  }

  du.copy_from(m_du_global);
}

/*! \brief Applies the transpose of the linearization of the forward map
 (i.e. the transpose of the reduced gradient \f$DF\f$ described in the class-level documentation.) */
/*!  As described previously,
\f[
Df = J_{\rm State}^{-1} J_{\rm Design}.
\f]
so
\f[
Df^t = J_{\rm Design}^t \; (J_{\rm State}^t)^{-1} .
\f]
Applying the transpose of the linearization then involves the solution of a linear equation.
The matrices \f$J_{\rm State}\f$ and \f$J_{\rm Design}\f$ both depend on the value of the
design variable \f$\zeta\f$ and the value of the corresponding state variable \f$u=F(\zeta)\f$.
These are established by first calling linearize_at.
  @param[in]   du     Perturbation of the state variable
  @param[out]  dzeta  Computed corresponding perturbation of the design variable; ghosts (if present) are updated.
*/
void IP_SSAHardavForwardProblem::apply_linearization_transpose(IceModelVec2V &du,
                                                               IceModelVec2S &dzeta) {

  PetscErrorCode ierr;

  if (m_rebuild_J_state) {
    this->assemble_jacobian_state(m_velocity, m_J_state);
    m_rebuild_J_state = false;
  }

  // Aliases to help with notation consistency below.
  const IceModelVec2Int *m_dirichletLocations = m_bc_mask;
  const IceModelVec2V   *m_dirichletValues    = m_bc_values;
  double        m_dirichletWeight    = m_dirichletScale;

  m_du_global.copy_from(du);
  Vector2 **du_a = m_du_global.get_array();
  fem::DirichletData_Vector dirichletBC;
  dirichletBC.init(m_dirichletLocations, m_dirichletValues, m_dirichletWeight);
  if (dirichletBC) {
    dirichletBC.fix_residual_homogeneous(du_a);
  }
  dirichletBC.finish();
  m_du_global.end_access();

  // call PETSc to solve linear system by iterative method.
#if PETSC_VERSION_LT(3,5,0)
  ierr = KSPSetOperators(m_ksp, m_J_state, m_J_state, SAME_NONZERO_PATTERN);
  PISM_CHK(ierr, "KSPSetOperators");
#else
  ierr = KSPSetOperators(m_ksp, m_J_state, m_J_state);
  PISM_CHK(ierr, "KSPSetOperators");
#endif
  ierr = KSPSolve(m_ksp, m_du_global.get_vec(), m_du_global.get_vec());
  PISM_CHK(ierr, "KSPSolve"); // SOLVE

  KSPConvergedReason  reason;
  ierr = KSPGetConvergedReason(m_ksp, &reason);
  PISM_CHK(ierr, "KSPGetConvergedReason");

  if (reason < 0) {
    throw RuntimeError::formatted("IP_SSAHardavForwardProblem::apply_linearization solve failed to converge (KSP reason %s)",
                                  KSPConvergedReasons[reason]);
  } else {
    verbPrintf(4, m_grid->com,
               "IP_SSAHardavForwardProblem::apply_linearization converged (KSP reason %s)\n",
               KSPConvergedReasons[reason]);
  }

  this->apply_jacobian_design_transpose(m_velocity, m_du_global, dzeta);
  dzeta.scale(-1);

  if (dzeta.get_stencil_width() > 0) {
    dzeta.update_ghosts();
  }
}

} // end of namespace inverse
} // end of namespace pism
