// Copyright (C) 2009--2014 Jed Brown and Ed Bueler and Constantine Khroulev and David Maxwell
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

#include "SSAFEM.hh"
#include "FETools.hh"
#include "Mask.hh"
#include "basal_resistance.hh"
#include "flowlaws.hh"

namespace pism {

typedef PetscErrorCode (*DMDASNESJacobianLocal)(DMDALocalInfo*, void*, Mat, Mat, MatStructure*, void*);
typedef PetscErrorCode (*DMDASNESFunctionLocal)(DMDALocalInfo*, void*, void*, void*);

SSA* SSAFEMFactory(IceGrid &g, EnthalpyConverter &ec, const Config &c) {
  return new SSAFEM(g, ec, c);
}

SSAFEM::SSAFEM(IceGrid &g, EnthalpyConverter &e, const Config &c)
  : SSA(g, e, c), m_element_index(g) {
  m_quadrature.init(grid);
  m_quadrature_vector.init(grid);
  PetscErrorCode ierr = allocate_fem();
  if (ierr != 0) {
    PetscPrintf(grid.com, "FATAL ERROR: SSAFEM allocation failed.\n");
    PISMEnd();
  }
}

SSAFEM::~SSAFEM() {
  deallocate_fem();
}

//! \brief Allocating SSAFEM-specific objects; called by the constructor.
PetscErrorCode SSAFEM::allocate_fem() {
  PetscErrorCode ierr;

  m_dirichletScale = 1.0;
  m_ocean_rho = config.get("sea_water_density");
  m_earth_grav = config.get("standard_gravity");
  m_beta_ice_free_bedrock = config.get("beta_ice_free_bedrock");

  ierr = SNESCreate(grid.com, &m_snes); CHKERRQ(ierr);

  // Set the SNES callbacks to call into our compute_local_function and compute_local_jacobian
  // methods via SSAFEFunction and SSAFEJ
  m_callback_data.da = SSADA;
  m_callback_data.ssa = this;
  ierr = DMDASNESSetFunctionLocal(SSADA, INSERT_VALUES, (DMDASNESFunctionLocal)SSAFEFunction, &m_callback_data); CHKERRQ(ierr);
  ierr = DMDASNESSetJacobianLocal(SSADA, (DMDASNESJacobianLocal)SSAFEJacobian, &m_callback_data); CHKERRQ(ierr);

  ierr = DMSetMatType(SSADA, "baij"); CHKERRQ(ierr);
  ierr = DMSetApplicationContext(SSADA, &m_callback_data); CHKERRQ(ierr);

  ierr = SNESSetDM(m_snes, SSADA); CHKERRQ(ierr);

  // Default of maximum 200 iterations; possibly overridded by commandline
  int snes_max_it = 200;
  ierr = SNESSetTolerances(m_snes, PETSC_DEFAULT, PETSC_DEFAULT, PETSC_DEFAULT,
                           snes_max_it, PETSC_DEFAULT);
  // ierr = SNESSetOptionsPrefix(snes, ((PetscObject)this)->prefix); CHKERRQ(ierr);

  ierr = SNESSetFromOptions(m_snes); CHKERRQ(ierr);

  // Allocate m_coefficients, which contains coefficient data at the quadrature points of all the elements.
  // There are nElement elements, and FEQuadrature::Nq quadrature points.
  int nElements = m_element_index.element_count();
  m_coefficients = new SSACoefficients[FEQuadrature::Nq*nElements];

  return 0;
}

//! Undo the allocations of SSAFEM::allocate_fem; called by the destructor.
PetscErrorCode SSAFEM::deallocate_fem() {
  PetscErrorCode ierr;

  ierr = SNESDestroy(&m_snes); CHKERRQ(ierr);
  delete[] m_coefficients;

  return 0;
}

// Initialize the solver, called once by the client before use.
PetscErrorCode SSAFEM::init(Vars &vars) {
  PetscErrorCode ierr;

  ierr = SSA::init(vars); CHKERRQ(ierr);
  ierr = verbPrintf(2, grid.com,
           "  [using the SNES-based finite element method implementation]\n");
           CHKERRQ(ierr);

  ierr = setFromOptions(); CHKERRQ(ierr);

  // On restart, SSA::init() reads the SSA velocity from a PISM output file
  // into IceModelVec2V "velocity". We use that field as an initial guess.
  // If we are not restarting from a PISM file, "velocity" is identically zero,
  // and the call below clears SSAX.

  ierr = m_velocity.copy_to_vec(SSAX); CHKERRQ(ierr);

  // Store coefficient data at the quadrature points.
  ierr = cacheQuadPtValues(); CHKERRQ(ierr);

  return 0;
}

//! Opportunity to modify behaviour based on command-line options.
/*! Called from SSAFEM::init */
PetscErrorCode SSAFEM::setFromOptions() {
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscOptionsHead("SSA FEM options"); CHKERRQ(ierr);
  m_dirichletScale = 1.0e9;
  ierr = PetscOptionsReal("-ssa_fe_dirichlet_scale",
                          "Enforce Dirichlet conditions with this additional scaling",
                          "",
                          m_dirichletScale,
                          &m_dirichletScale, NULL); CHKERRQ(ierr);
  ierr = PetscOptionsTail(); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

//! Solve the SSA.  The FEM solver exchanges time for memory by computing
//! the value of the various coefficients at each of the quadrature points
//! only once.  When running in an ice-model context, at each time step,
//! SSA::update is called, which calls SSAFEM::solve.  Since coefficients
//! have generally changed between timesteps, we need to recompute coefficeints
//! at the quad points. On the other hand, in the context of inversion,
//! coefficients will not change between iteration and there is no need to
//! recompute the values at the quad points.  So there are two different solve
//! methods, SSAFEM::solve() and SSAFEM::solve_nocache().  The only difference
//! is that SSAFEM::solve() recomputes the cached values of the coefficients at
//! quadrature points before calling SSAFEM::solve_nocache().
PetscErrorCode SSAFEM::solve() {
  PetscErrorCode ierr;

  // Set up the system to solve (store coefficient data at the quadrature points):
  ierr = cacheQuadPtValues(); CHKERRQ(ierr);

  TerminationReason::Ptr reason;
  ierr = solve_nocache(reason); CHKERRQ(ierr);
  if (reason->failed())
  {
    SETERRQ1(grid.com, 1,
    "SSAFEM solve failed to converge (SNES reason %s)\n\n", reason->description().c_str());
  }
  else if (getVerbosityLevel() > 2)
  {
    stdout_ssa += "SSAFEM converged (SNES reason ";
    stdout_ssa += reason->description();
    stdout_ssa += ")\n";
  }

  return 0;
}

PetscErrorCode SSAFEM::solve(TerminationReason::Ptr &reason) {
  PetscErrorCode ierr;

  // Set up the system to solve (store coefficient data at the quadrature points):
  ierr = cacheQuadPtValues(); CHKERRQ(ierr);

  ierr = solve_nocache(reason); CHKERRQ(ierr);

  return 0;
}

//! Solve the SSA without first recomputing the values of coefficients at quad
//! points.  See the disccusion of SSAFEM::solve for more discussion.
PetscErrorCode SSAFEM::solve_nocache(TerminationReason::Ptr &reason) {
  PetscErrorCode ierr;
  PetscViewer    viewer;
  char           filename[PETSC_MAX_PATH_LEN];
  PetscBool     flg;

  m_epsilon_ssa = config.get("epsilon_ssa");

  ierr = PetscOptionsGetString(NULL, "-ssa_view", filename,
                               PETSC_MAX_PATH_LEN, &flg); CHKERRQ(ierr);
  if (flg) {
    ierr = PetscViewerASCIIOpen(grid.com, filename, &viewer);
             CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer, "SNES before SSASolve_FE\n");
             CHKERRQ(ierr);
    ierr = SNESView(m_snes, viewer); CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer, "solution vector before SSASolve_FE\n");
             CHKERRQ(ierr);
    ierr = VecView(SSAX, viewer); CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer); CHKERRQ(ierr);
  }

  stdout_ssa.clear();
  if (getVerbosityLevel() >= 2)
    stdout_ssa = "  SSA: ";

  // Solve:
  ierr = SNESSolve(m_snes, NULL, SSAX); CHKERRQ(ierr);

  // See if it worked.
  SNESConvergedReason snes_reason;
  ierr = SNESGetConvergedReason(m_snes, &snes_reason); CHKERRQ(ierr);
  reason.reset(new SNESTerminationReason(snes_reason));
  if (reason->failed()) {
    return 0;
  }

  // Extract the solution back from SSAX to velocity and communicate.
  ierr = m_velocity.copy_from_vec(SSAX); CHKERRQ(ierr);
  ierr = m_velocity.update_ghosts(); CHKERRQ(ierr);

  ierr = PetscOptionsHasName(NULL, "-ssa_view_solution", &flg); CHKERRQ(ierr);
  if (flg) {
    ierr = PetscViewerASCIIOpen(grid.com, filename, &viewer); CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer, "solution vector after SSASolve\n");
             CHKERRQ(ierr);
    ierr = VecView(SSAX, viewer); CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer); CHKERRQ(ierr);
  }
  return 0;
}

//! Initialize stored data from the coefficients in the SSA.  Called by SSAFEM::solve.
/* This method is should be called after SSAFEM::init and whenever
any geometry or temperature related coefficients have changed. The method
stores the values of the coefficients at the quadrature points of each
element so that these interpolated values do not need to be computed
during each outer iteration of the nonlinear solve.*/
PetscErrorCode SSAFEM::cacheQuadPtValues() {
  double
    *Enth_e[4],
    *Enth_q[4];

  int i, j, q, p;
  unsigned int k = 0;

  PetscErrorCode   ierr;

  double ice_density = config.get("ice_density");

  for (q=0; q<FEQuadrature::Nq; q++)
  {
    Enth_q[q] = new double[grid.Mz];
  }

  GeometryCalculator gc(sea_level, config);

  ierr = enthalpy->begin_access(); CHKERRQ(ierr);
  bool driving_stress_explicit;
  if ((driving_stress_x != NULL) && (driving_stress_y != NULL)) {
    driving_stress_explicit = true;
    ierr = driving_stress_x->begin_access(); CHKERRQ(ierr);
    ierr = driving_stress_y->begin_access(); CHKERRQ(ierr);
  } else {
    // The class SSA ensures in this case that 'surface' is available
    driving_stress_explicit = false;
    ierr = surface->begin_access(); CHKERRQ(ierr);
  }

  ierr = thickness->begin_access(); CHKERRQ(ierr);
  ierr = bed->begin_access(); CHKERRQ(ierr);
  ierr = tauc->begin_access(); CHKERRQ(ierr);

  int xs = m_element_index.xs, xm = m_element_index.xm,
    ys   = m_element_index.ys, ym = m_element_index.ym;

  for (i=xs; i<xs+xm; i++) {
    for (j=ys; j<ys+ym; j++) {
      double hq[FEQuadrature::Nq], hxq[FEQuadrature::Nq], hyq[FEQuadrature::Nq];
      double ds_xq[FEQuadrature::Nq], ds_yq[FEQuadrature::Nq];
      if (driving_stress_explicit) {
        m_quadrature.computeTrialFunctionValues(i, j, m_dofmap, *driving_stress_x, ds_xq);
        m_quadrature.computeTrialFunctionValues(i, j, m_dofmap, *driving_stress_y, ds_yq);
      } else {
        // Extract coefficient values at the quadrature points.
        m_quadrature.computeTrialFunctionValues(i, j, m_dofmap, *surface, hq, hxq, hyq);
      }

      double Hq[FEQuadrature::Nq], bq[FEQuadrature::Nq], taucq[FEQuadrature::Nq];
      m_quadrature.computeTrialFunctionValues(i, j, m_dofmap, *thickness, Hq);
      m_quadrature.computeTrialFunctionValues(i, j, m_dofmap, *bed, bq);
      m_quadrature.computeTrialFunctionValues(i, j, m_dofmap, *tauc, taucq);

      const int ij = m_element_index.flatten(i, j);
      SSACoefficients *coefficients = &m_coefficients[4*ij];
      for (q = 0; q < FEQuadrature::Nq; q++) {
        coefficients[q].H  = Hq[q];
        coefficients[q].b  = bq[q];
        coefficients[q].tauc = taucq[q];
        if (driving_stress_explicit) {
          coefficients[q].driving_stress.u = ds_xq[q];
          coefficients[q].driving_stress.v = ds_yq[q];
        } else {
          coefficients[q].driving_stress.u = -ice_density*m_earth_grav*Hq[q]*hxq[q];
          coefficients[q].driving_stress.v = -ice_density*m_earth_grav*Hq[q]*hyq[q];
        }

        coefficients[q].mask = gc.mask(coefficients[q].b, coefficients[q].H);
      }

      // In the following, we obtain the averaged hardness value from enthalpy by
      // interpolating enthalpy in each column over a quadrature point and then
      // taking the average over the column.  A faster approach would be to take
      // the column average over each element nodes and then interpolate to the
      // quadrature points. Does this make a difference?

      // Obtain the values of enthalpy at each vertical level at each of the vertices
      // of the current element.
      ierr = enthalpy->getInternalColumn(i, j, &Enth_e[0]);
      ierr = enthalpy->getInternalColumn(i+1, j, &Enth_e[1]); CHKERRQ(ierr);
      ierr = enthalpy->getInternalColumn(i+1, j+1, &Enth_e[2]); CHKERRQ(ierr);
      ierr = enthalpy->getInternalColumn(i, j+1, &Enth_e[3]); CHKERRQ(ierr);

      // We now want to interpolate to the quadrature points at each of the
      // vertical levels.  It would be nice to use quadrature::computeTestFunctionValues,
      // but the way we have just obtained the values at the element vertices
      // using getInternalColumn doesn't make this straightforward.  So we compute the values
      // by hand.
      const FEFunctionGerm (*test)[FEQuadrature::Nk] = m_quadrature.testFunctionValues();
      for (k = 0; k < grid.Mz; k++) {
        Enth_q[0][k] = Enth_q[1][k] = Enth_q[2][k] = Enth_q[3][k] = 0;
        for (q = 0; q < FEQuadrature::Nq; q++) {
          for (p = 0; p < FEQuadrature::Nk; p++) {
            Enth_q[q][k] += test[q][p].val * Enth_e[p][k];
          }
        }
      }

      // Now, for each column over a quadrature point, find the averaged_hardness.
      for (q = 0; q < FEQuadrature::Nq; q++) {
        // Evaluate column integrals in flow law at every quadrature point's column
        coefficients[q].B = flow_law->averaged_hardness(coefficients[q].H, grid.kBelowHeight(coefficients[q].H),
                                                        &grid.zlevels[0], Enth_q[q]);
      }
    }
  }
  if (driving_stress_explicit) {
    ierr = driving_stress_x->end_access(); CHKERRQ(ierr);
    ierr = driving_stress_y->end_access(); CHKERRQ(ierr);
  } else {
    ierr = surface->end_access(); CHKERRQ(ierr);
  }
  ierr = thickness->end_access(); CHKERRQ(ierr);
  ierr = bed->end_access(); CHKERRQ(ierr);
  ierr = tauc->end_access(); CHKERRQ(ierr);
  ierr = enthalpy->end_access(); CHKERRQ(ierr);

  for (q = 0; q < FEQuadrature::Nq; q++)
  {
    delete [] Enth_q[q];
  }

  return 0;
}

/** @brief Compute the "2 x (effective viscosity) x (ice thickness)"
 *  and effective viscous bed strength from the current solution, at a
 *  single quadrature point.
 *
 * @param[in] coefficients SSA coefficients at the current quadrature point
 * @param[in] u the value of the solution
 * @param[in] Du the value of the symmetric gradient of the solution
 * @param[out] nuH product of the ice viscosity and thickness @f$ \nu H @f$
 * @param[out] dNuH derivative of @f$ \nu H @f$ with respect to the
 *                  second invariant @f$ \gamma @f$. Set to NULL if
 *                  not desired.
 * @param[out] beta basal drag coefficient @f$ \beta @f$
 * @param[out] dbeta derivative of @f$ \beta @f$ with respect to the
 *                   second invariant @f$ \gamma @f$. Set to NULL if
 *                   not desired.
 *
 * @return 0 on success
 */
PetscErrorCode SSAFEM::PointwiseNuHAndBeta(const SSACoefficients *coefficients,
                                           const Vector2 &u, const double Du[],
                                           double *nuH, double *dNuH,
                                           double *beta, double *dbeta) {

  Mask M;

  if (coefficients->H < strength_extension->get_min_thickness()) {
    *nuH = strength_extension->get_notional_strength();
    if (dNuH) *dNuH = 0;
  } else {
    flow_law->effective_viscosity(coefficients->B, secondInvariantDu_2D(Du),
                                  nuH, dNuH);
    *nuH  *= coefficients->H;
    *nuH  += m_epsilon_ssa;
    if (dNuH) *dNuH *= coefficients->H;
  }
  *nuH  *=  2;
  if (dNuH) {
    *dNuH *= 2;
  }

  if (M.grounded_ice(coefficients->mask)) {
    basal_sliding_law->drag_with_derivative(coefficients->tauc, u.u, u.v, beta, dbeta);
  } else {
    *beta = 0;

    if (M.ice_free_land(coefficients->mask)) {
      *beta = m_beta_ice_free_bedrock;
    }

    if (dbeta) {
      *dbeta = 0;
    }
  }
  return 0;
}

//! \brief Sets Dirichlet boundary conditions. Called from SSAFEFunction and
//! SSAFEJacobian.
/*! If for some vertex \a local_bc_mask indicates that it
is an explicit Dirichlet node, the values of x for that node is set from the Dirichlet
data BC_vel. The row and column in the \a dofmap are set as invalid.
This last step ensures that the residual and Jacobian entries
corresponding to a Dirichlet unknown are not set in the main loops of
SSAFEM::compute_local_function and SSSAFEM:compute_local_jacobian.
*/
void SSAFEM::FixDirichletValues(double local_bc_mask[], IceModelVec2V &BC_vel,
                                Vector2 x[], FEDOFMap &my_dofmap) {
  for (int k = 0; k < FEQuadrature::Nk; k++) {
    if (local_bc_mask[k] > 0.5) { // Dirichlet node
      int ii, jj;
      my_dofmap.localToGlobal(k, &ii, &jj);
      x[k].u = BC_vel(ii, jj).u;
      x[k].v = BC_vel(ii, jj).v;
      // Mark any kind of Dirichlet node as not to be touched
      my_dofmap.markRowInvalid(k);
      my_dofmap.markColInvalid(k);
    }
  }
}

//! Implements the callback for computing the SNES local function.
/*! Compute the residual \f[r_{ij}= G(x, \psi_{ij}) \f] where \f$G\f$ is the weak form of the SSA, \f$x\f$
is the current approximate solution, and the \f$\psi_{ij}\f$ are test functions. */
PetscErrorCode SSAFEM::compute_local_function(DMDALocalInfo *info,
                                              const Vector2 **velocity_global,
                                              Vector2 **residual_global) {
  PetscErrorCode   ierr;

  (void) info; // Avoid compiler warning.

  // Zero out the portion of the function we are responsible for computing.
  for (int i = grid.xs; i < grid.xs + grid.xm; i++) {
    for (int j = grid.ys; j < grid.ys + grid.ym; j++) {
      residual_global[i][j].u = 0.0;
      residual_global[i][j].v = 0.0;
    }
  }

  // Start access of Dirichlet data, if present.
  if (bc_locations && m_vel_bc) {
    ierr = bc_locations->begin_access(); CHKERRQ(ierr);
    ierr = m_vel_bc->begin_access(); CHKERRQ(ierr);
  }

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  // Storage for the current solution at quadrature points.
  Vector2 u[FEQuadrature::Nq];
  double Du[FEQuadrature::Nq][3];

  // An Nq by Nk array of test function values.
  const FEFunctionGerm (*test)[FEQuadrature::Nk] = m_quadrature.testFunctionValues();

  // Flags for each vertex in an element that determine if explicit Dirichlet data has
  // been set.
  double local_bc_mask[FEQuadrature::Nk];

  // Iterate over the elements.
  int xs = m_element_index.xs, xm = m_element_index.xm,
    ys = m_element_index.ys, ym = m_element_index.ym;
  for (int i = xs; i < xs + xm; i++) {
    for (int j = ys; j < ys + ym; j++) {
      // Storage for element-local solution and residuals.
      Vector2     velocity[4], residual[4];
      // Index into coefficient storage in m_coefficients
      const int ij = m_element_index.flatten(i, j);

      // Initialize the map from global to local degrees of freedom for this element.
      m_dofmap.reset(i, j, grid);

      // Obtain the value of the solution at the nodes adjacent to the element.
      m_dofmap.extractLocalDOFs(i, j, velocity_global, velocity);

      // These values now need to be adjusted if some nodes in the element have
      // Dirichlet data.
      if (bc_locations && m_vel_bc) {
        m_dofmap.extractLocalDOFs(i, j, *bc_locations, local_bc_mask);
        FixDirichletValues(local_bc_mask, *m_vel_bc, velocity, m_dofmap);
      }

      // Zero out the element-local residual in prep for updating it.
      for (unsigned int k = 0; k < FEQuadrature::Nk; k++){
        residual[k].u = 0;
        residual[k].v = 0;
      }

      // Compute the solution values and symmetric gradient at the quadrature points.
      m_quadrature_vector.computeTrialFunctionValues(velocity, u, Du);

      // Coefficients and weights for this quadrature point.
      const SSACoefficients *coefficients = &m_coefficients[ij*FEQuadrature::Nq];

      for (unsigned int q = 0; q < FEQuadrature::Nq; q++) {     // loop over quadrature points on this element.

        // Symmetric gradient at the quadrature point.
        double *Duq = Du[q];

        // Coefficients and weights for this quadrature point.
        const double    jw = JxW[q];
        double nuH = 0.0, beta = 0.0;
        ierr = PointwiseNuHAndBeta(&coefficients[q], u[q], Duq,
                                   &nuH, NULL, &beta, NULL); CHKERRQ(ierr);

        // The next few lines compute the actual residual for the element.
        const Vector2
          tau_b = u[q] * (- beta), // basal shear stress
          tau_d = coefficients[q].driving_stress, // gravitational driving stress
          f     = tau_b + tau_d;

        for (unsigned int k = 0; k < FEQuadrature::Nk; k++) {  // loop over the test functions.
          const FEFunctionGerm &testqk = test[q][k];
          residual[k].u += jw*(nuH*(testqk.dx*(2*Duq[0] + Duq[1]) + testqk.dy*Duq[2]) - testqk.val*f.u);
          residual[k].v += jw*(nuH*(testqk.dy*(2*Duq[1] + Duq[0]) + testqk.dx*Duq[2]) - testqk.val*f.v);
        }
      } // q

      m_dofmap.addLocalResidualBlock(residual, residual_global);
    } // j-loop
  } // i-loop

  // Until now we have not touched rows in the residual corresponding to Dirichlet data.
  // We fix this now.
  if (bc_locations && m_vel_bc) {
    // Enforce Dirichlet conditions strongly
    for (int i = grid.xs; i < grid.xs + grid.xm; i++) {
      for (int j = grid.ys; j < grid.ys + grid.ym; j++) {
        if ((*bc_locations)(i, j) > 0.5) {
          // Enforce explicit dirichlet data.
          residual_global[i][j].u = m_dirichletScale * (velocity_global[i][j].u - (*m_vel_bc)(i, j).u);
          residual_global[i][j].v = m_dirichletScale * (velocity_global[i][j].v - (*m_vel_bc)(i, j).v);
        }
      }
    }
    ierr = bc_locations->end_access(); CHKERRQ(ierr);
    ierr = m_vel_bc->end_access(); CHKERRQ(ierr);
  }

  PetscBool monitorFunction;
  ierr = PetscOptionsHasName(NULL, "-ssa_monitor_function", &monitorFunction); CHKERRQ(ierr);
  if (monitorFunction) {
    ierr = PetscPrintf(grid.com, "SSA Solution and Function values (pointwise residuals)\n"); CHKERRQ(ierr);
    for (int i = grid.xs; i < grid.xs + grid.xm; i++) {
      for (int j = grid.ys; j < grid.ys + grid.ym; j++) {
        ierr = PetscSynchronizedPrintf(grid.com,
                                       "[%2d, %2d] u=(%12.10e, %12.10e)  f=(%12.4e, %12.4e)\n",
                                       i, j,
                                       velocity_global[i][j].u, velocity_global[i][j].v,
                                       residual_global[i][j].u, residual_global[i][j].v);
        CHKERRQ(ierr);
      }
    }
    ierr = PetscSynchronizedFlush(grid.com); CHKERRQ(ierr);
  }

  return 0;
}



//! Implements the callback for computing the SNES local Jacobian.
/*! Compute the Jacobian \f[J_{ij}{kl} \frac{d r_{ij}}{d x_{kl}}= G(x, \psi_{ij}) \f]
where \f$G\f$ is the weak form of the SSA, \f$x\f$ is the current approximate solution, and
the \f$\psi_{ij}\f$ are test functions. */
PetscErrorCode SSAFEM::compute_local_jacobian(DMDALocalInfo *info,
                                              const Vector2 **velocity_global, Mat Jac) {
  int         i, j;
  PetscErrorCode   ierr;

  // Avoid compiler warning.
  (void) info;

  // Zero out the Jacobian in preparation for updating it.
  ierr = MatZeroEntries(Jac); CHKERRQ(ierr);

  // Start access to Dirichlet data if present.
  if (bc_locations && m_vel_bc) {
    ierr = bc_locations->begin_access(); CHKERRQ(ierr);
    ierr = m_vel_bc->begin_access(); CHKERRQ(ierr);
  }

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  // Storage for the current solution at quadrature points.
  Vector2 w[FEQuadrature::Nq];
  double Dw[FEQuadrature::Nq][3];

  // Values of the finite element test functions at the quadrature points.
  // This is an Nq by Nk array of function germs (Nq=#of quad pts, Nk=#of test functions).
  const FEFunctionGerm (*test)[FEQuadrature::Nk] = m_quadrature.testFunctionValues();

  // Flags for each vertex in an element that determine if explicit Dirichlet data has
  // been set.
  double local_bc_mask[FEQuadrature::Nk];

  // Loop through all the elements.
  int xs = m_element_index.xs, xm = m_element_index.xm,
           ys = m_element_index.ys, ym = m_element_index.ym;
  for (i=xs; i<xs+xm; i++) {
    for (j=ys; j<ys+ym; j++) {
      // Values of the solution at the nodes of the current element.
      Vector2    velocity[FEQuadrature::Nk];

      // Element-local Jacobian matrix (there are FEQuadrature::Nk vector valued degrees
      // of freedom per elment, for a total of (2*FEQuadrature::Nk)*(2*FEQuadrature::Nk) = 16
      // entries in the local Jacobian.
      double      K[(2*FEQuadrature::Nk)*(2*FEQuadrature::Nk)];

      // Index into the coefficient storage array.
      const int ij = m_element_index.flatten(i, j);

      // Initialize the map from global to local degrees of freedom for this element.
      m_dofmap.reset(i, j, grid);

      // Obtain the value of the solution at the adjacent nodes to the element.
      m_dofmap.extractLocalDOFs(i, j, velocity_global, velocity);

      // These values now need to be adjusted if some nodes in the element have
      // Dirichlet data.
      if (bc_locations && m_vel_bc) {
        m_dofmap.extractLocalDOFs(i, j, *bc_locations, local_bc_mask);
        FixDirichletValues(local_bc_mask, *m_vel_bc, velocity, m_dofmap);
      }

      // Compute the values of the solution at the quadrature points.
      m_quadrature_vector.computeTrialFunctionValues(velocity, w, Dw);

      // Build the element-local Jacobian.
      ierr = PetscMemzero(K, sizeof(K)); CHKERRQ(ierr);
      for (int q = 0; q < FEQuadrature::Nq; q++) {

        // Shorthand for values and derivatives of the solution at the single quadrature point.
        Vector2 &wq = w[q];
        double *Dwq = Dw[q];

        // Coefficients evaluated at the single quadrature point.
        const SSACoefficients *coefficients = &m_coefficients[ij*4+q];
        const double    jw  = JxW[q];
        double nuH, dNuH, beta, dbeta;
        ierr = PointwiseNuHAndBeta(coefficients, wq, Dwq,
                                   &nuH, &dNuH, &beta, &dbeta); CHKERRQ(ierr);

        for (int k = 0; k < FEQuadrature::Nk; k++) {   // Test functions
          for (int l = 0; l < FEQuadrature::Nk; l++) { // Trial functions

            // FIXME (DAM 2/28/11) The following computations could be a little better documented.
            const FEFunctionGerm &test_qk=test[q][k];
            const FEFunctionGerm &test_ql=test[q][l];

            const double ht = test_qk.val, h = test_ql.val,
                  dxt = test_qk.dx, dyt = test_qk.dy,
                  dx = test_ql.dx, dy = test_ql.dy,

            // Cross terms appearing with beta'
            bvx = ht*wq.u, bvy = ht*wq.v, bux = wq.u*h, buy = wq.v*h,
            // Cross terms appearing with nuH'
            cvx = dxt*(2*Dwq[0]+Dwq[1]) + dyt*Dwq[2],
            cvy = dyt*(2*Dwq[1]+Dwq[0]) + dxt*Dwq[2],
            cux = (2*Dwq[0]+Dwq[1])*dx + Dwq[2]*dy,
            cuy = (2*Dwq[1]+Dwq[0])*dy + Dwq[2]*dx;

            if (nuH==0)
            {
              verbPrintf(1, grid.com, "nuh=0 i %d j %d q %d k %d\n", i, j, q, k);
            }
            // u-u coupling
            K[k*16+l*2]     += jw*(beta*ht*h + dbeta*bvx*bux + nuH*(2*dxt*dx + dyt*0.5*dy) + dNuH*cvx*cux);
            // u-v coupling
            K[k*16+l*2+1]   += jw*(dbeta*bvx*buy + nuH*(0.5*dyt*dx + dxt*dy) + dNuH*cvx*cuy);
            // v-u coupling
            K[k*16+8+l*2]   += jw*(dbeta*bvy*bux + nuH*(0.5*dxt*dy + dyt*dx) + dNuH*cvy*cux);
            // v-v coupling
            K[k*16+8+l*2+1] += jw*(beta*ht*h + dbeta*bvy*buy + nuH*(2*dyt*dy + dxt*0.5*dx) + dNuH*cvy*cuy);
          } // l
        } // k
      } // q
      ierr = m_dofmap.addLocalJacobianBlock(K, Jac);
    } // j
  } // i


  // Until now, the rows and columns correspoinding to Dirichlet data have not been set.  We now
  // put an identity block in for these unknowns.  Note that because we have takes steps to not touching these
  // columns previously, the symmetry of the Jacobian matrix is preserved.
  if (bc_locations && m_vel_bc) {
    for (i=grid.xs; i<grid.xs+grid.xm; i++) {
      for (j=grid.ys; j<grid.ys+grid.ym; j++) {
        if (bc_locations->as_int(i, j) == 1) {
          const double ident[4] = {m_dirichletScale, 0, 0, m_dirichletScale};
          MatStencil row;
          // FIXME: Transpose shows up here!
          row.j = i; row.i = j;
          ierr = MatSetValuesBlockedStencil(Jac, 1, &row, 1, &row, ident, ADD_VALUES); CHKERRQ(ierr);
        }
      }
    }
  }

  if (bc_locations) {
    ierr = bc_locations->end_access(); CHKERRQ(ierr);
  }
  if (m_vel_bc) {
    ierr = m_vel_bc->end_access(); CHKERRQ(ierr);
  }

  ierr = MatAssemblyBegin(Jac, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(Jac, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);

  PetscBool monitor_jacobian;
  ierr = PetscOptionsHasName(NULL, "-ssa_monitor_jacobian", &monitor_jacobian); CHKERRQ(ierr);
  if (monitor_jacobian) {
    PetscViewer    viewer;

    char           file_name[PETSC_MAX_PATH_LEN];
    int iter;
    ierr = SNESGetIterationNumber(m_snes, &iter);
    snprintf(file_name, PETSC_MAX_PATH_LEN, "PISM_SSAFEM_J%d.m", iter);

      ierr = verbPrintf(2, grid.com,
                 "writing Matlab-readable file for SSAFEM system A xsoln = rhs to file `%s' ...\n",
                 file_name); CHKERRQ(ierr);
      ierr = PetscViewerCreate(grid.com, &viewer); CHKERRQ(ierr);
      ierr = PetscViewerSetType(viewer, PETSCVIEWERASCII); CHKERRQ(ierr);
      ierr = PetscViewerSetFormat(viewer, PETSC_VIEWER_ASCII_MATLAB); CHKERRQ(ierr);
      ierr = PetscViewerFileSetName(viewer, file_name); CHKERRQ(ierr);

      ierr = PetscObjectSetName((PetscObject) Jac, "A"); CHKERRQ(ierr);
      ierr = MatView(Jac, viewer); CHKERRQ(ierr);
  }

  ierr = MatSetOption(Jac, MAT_NEW_NONZERO_LOCATION_ERR, PETSC_TRUE); CHKERRQ(ierr);
  ierr = MatSetOption(Jac, MAT_SYMMETRIC, PETSC_TRUE); CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

//!
PetscErrorCode SSAFEFunction(DMDALocalInfo *info,
			     const Vector2 **velocity, Vector2 **residual,
			     SSAFEM_SNESCallbackData *fe) {
  return fe->ssa->compute_local_function(info, velocity, residual);
}

PetscErrorCode SSAFEJacobian(DMDALocalInfo *info, const Vector2 **velocity,
			     Mat A, Mat J, MatStructure *str, SSAFEM_SNESCallbackData *fe) {

  (void) A;

  PetscErrorCode ierr = fe->ssa->compute_local_jacobian(info, velocity, J); CHKERRQ(ierr);

  *str = SAME_NONZERO_PATTERN;

  return 0;
}

} // end of namespace pism
