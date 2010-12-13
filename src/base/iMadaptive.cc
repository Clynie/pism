// Copyright (C) 2004-2010 Jed Brown, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
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

#include "iceModel.hh"
#include <petscvec.h>


//! Compute the maximum diffusivity associated to the SIA deformational velocity.
/*! 
The time-stepping scheme for mass continuity is explicit.  It solves an equation
which in the simple case of flat bed and the use of the nonsliding SIA is a
pure diffusion,
  \f[ H_t = M - S - \nabla \cdot \mathbf{Q} \f]
where
  \f[ \mathbf{Q} = - D \nabla H \f]
is the horizontal ice flux and \f$D\ge 0\f$ is the diffusivity.  Because the PDE
is actually nonlinear, this diffusivity \f$D\f$ changes at every time step.  The 
current procedure computes the maximum of the diffusivity on the grid.

Note that more generally the ice flow is driven by the driving stress
\f$\tau_d = - \rho g H \nabla h\f$ which involves the surface slope, not the
gradient of the thickness \f$H\f$.

The time-stepping for the explicit scheme is controlled by equation (25) in
[\ref BBL], so that \f$\Delta t \sim \frac{\Delta x^2}{\max D}\f$; see also
[\ref MortonMayers].  But also
  \f[ \mathbf{Q} = \bar U\, H \f]
where \f$\bar U\f$ is the vertically-averaged horizontal velocity.  Because of
how the SIA calculation is currently factored, we compute \f$D\f$ here, for the
purpose of adaptive time stepping, by the formula
  \f[ D = \frac{|\bar U| H}{|\nabla h|}. \f]
The potential division by zero in (harmless) areas of flat slope is avoided by
addition of a constant to \f$\alpha = |\nabla h|\f$.

See surfaceGradientSIA(), velocitySIAStaggered(), determineTimeStep(), and
massContExplicitStep(); all of these are related calculations.

This method assumes IceModelVec2Stag uvbar holds correct deformational values of
velocities coming out of the calculation in velocitySIAStaggered().  It also
assumes the thickness in vH is up to date.

This method puts the maximum, over all staggered points, of the diffusivities
into the global variable IceModel::gDmax.  

If the user wants a run-time view of diffusivity, then that is updated here.
 */
PetscErrorCode IceModel::computeMaxDiffusivity(bool update_diffusivity_viewer) {
  PetscErrorCode ierr;
  PetscScalar Dmax = 0.0;

  if (computeSIAVelocities == PETSC_TRUE) {

    // FIXME:  there is a better way to do this, namely to compute the
    //         diffusivity inside velocitySIAStaggered(); as it is we call
    //         surfaceGradientSIA() twice

    // uses:
    // * thickness and bed topography or
    // * usurf
    // updates: surface gradient in vWork2d[0,1,2,3], including w=1 ghosts
    ierr = surfaceGradientSIA(); CHKERRQ(ierr); // comm may happen here
    // surface gradient temporarily stored in vWork2d[0 1 2 3] 

    const PetscScalar DEFAULT_ADDED_TO_SLOPE_FOR_DIFF_IN_ADAPTIVE = 1.0e-4;
    PetscScalar **D, **h_x0, **h_y1;
    ierr = vWork2d[0].get_array(h_x0); CHKERRQ(ierr);
    ierr = vWork2d[3].get_array(h_y1); CHKERRQ(ierr);
    ierr = vH.begin_access(); CHKERRQ(ierr);
    ierr = uvbar.begin_access(); CHKERRQ(ierr);
    ierr = vWork2d[4].get_array(D); CHKERRQ(ierr);
    for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
      for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
        if ( (vH(i,j) > 0.0) || (vH(i+1,j) > 0.0) || (vH(i,j+1) > 0.0) ){
          // note that in velocitySIAStaggered() we see this:
          //      uvbar(i,j,o) = - Dfoffset * slope / thickness
          // where
          //      slope = (o==0) ? h_x[o][i][j] : h_y[o][i][j]
          // we undo the calculation
          // we get everything on the staggered grid points
          // (i+1/2,j) and (i,j+1/2)
          const PetscScalar
             H0 = 0.5 * (vH(i,j) + vH(i+1,j)),
             H1 = 0.5 * (vH(i,j) + vH(i,j+1)),
             slope0 = PetscAbs(h_x0[i][j]) + DEFAULT_ADDED_TO_SLOPE_FOR_DIFF_IN_ADAPTIVE,
             slope1 = PetscAbs(h_y1[i][j]) + DEFAULT_ADDED_TO_SLOPE_FOR_DIFF_IN_ADAPTIVE,
             D0 = (H0 * PetscAbs(uvbar(i,j,0))) / slope0,
             D1 = (H1 * PetscAbs(uvbar(i,j,1))) / slope1;
          const PetscScalar  d = PetscMax(D0,D1);  // max of two staggered vals
          D[i][j] = d;
          if (d > Dmax) Dmax = d;
        } else {
          D[i][j] = 0.0; // no diffusivity if no ice; this is consistent with
                         // degenerate diffusivity interpretation
        }
      }
    }
    ierr = vH.end_access(); CHKERRQ(ierr);  
    ierr = uvbar.end_access(); CHKERRQ(ierr);
    ierr = vWork2d[0].end_access(); CHKERRQ(ierr);
    ierr = vWork2d[3].end_access(); CHKERRQ(ierr);
    ierr = vWork2d[4].end_access(); CHKERRQ(ierr);

  } else {
    // if no SIA calculation at all
    ierr = vWork2d[4].set(0.0); CHKERRQ(ierr);
    Dmax = 0.0;
  }
  
  if (update_diffusivity_viewer) { // view diffusivity (m^2/s)
    ierr = vWork2d[4].set_name("diffusivity"); CHKERRQ(ierr);
    ierr = vWork2d[4].set_attrs("diagnostic",
				"diffusivity", "m2/s", ""); CHKERRQ(ierr);
    ierr = vWork2d[4].view(300); CHKERRQ(ierr);
  }

  ierr = PetscGlobalMax(&Dmax, &gDmax, grid.com); CHKERRQ(ierr);
  return 0;
}


//! Compute the maximum velocities for time-stepping and reporting to user.
/*!
Computes the maximum magnitude of the components \f$u,v,w\f$ of the 3D velocity.
Then sets \c CFLmaxdt, the maximum time step allowed under the 
Courant-Friedrichs-Lewy (CFL) condition on the 
horizontal advection scheme for age and for temperature.

Under BOMBPROOF there is no CFL condition for the vertical advection.
The maximum vertical velocity is computed but it does not affect
\c CFLmaxdt.
 */
PetscErrorCode IceModel::computeMax3DVelocities() {
  PetscErrorCode ierr;
  PetscScalar **H, *u, *v, *w;
  PetscScalar locCFLmaxdt = config.get("maximum_time_step_years") * secpera;

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = u3.begin_access(); CHKERRQ(ierr);
  ierr = v3.begin_access(); CHKERRQ(ierr);
  ierr = w3.begin_access(); CHKERRQ(ierr);

  // update global max of abs of velocities for CFL; only velocities under surface
  PetscReal   maxu=0.0, maxv=0.0, maxw=0.0;
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscInt      ks = grid.kBelowHeight(H[i][j]);
      ierr = u3.getInternalColumn(i,j,&u); CHKERRQ(ierr);
      ierr = v3.getInternalColumn(i,j,&v); CHKERRQ(ierr);
      ierr = w3.getInternalColumn(i,j,&w); CHKERRQ(ierr);
      for (PetscInt k=0; k<ks; ++k) {
        const PetscScalar absu = PetscAbs(u[k]),
                          absv = PetscAbs(v[k]);
        maxu = PetscMax(maxu,absu);
        maxv = PetscMax(maxv,absv);
        // make sure the denominator below is positive:
        PetscScalar tempdenom = (0.001/secpera) / (grid.dx + grid.dy);  
        tempdenom += PetscAbs(absu/grid.dx) + PetscAbs(absv/grid.dy);
        locCFLmaxdt = PetscMin(locCFLmaxdt,1.0 / tempdenom); 
        maxw = PetscMax(maxw, PetscAbs(w[k]));        
      }
    }
  }

  ierr = u3.end_access(); CHKERRQ(ierr);
  ierr = v3.end_access(); CHKERRQ(ierr);
  ierr = w3.end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);

  ierr = PetscGlobalMax(&maxu, &gmaxu, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&maxv, &gmaxv, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&maxw, &gmaxw, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMin(&locCFLmaxdt, &CFLmaxdt, grid.com); CHKERRQ(ierr);
  return 0;
}


//! Compute the CFL constant associated to first-order upwinding for the sliding contribution to mass continuity.
/*!
This procedure computes the maximum horizontal speed in the SSA areas.  In
particular it computes CFL constant for the upwinding, in massContExplicitStep(),
which applies to the basal component of mass flux.

That is, because the map-plane mass continuity is advective in the
sliding case we have a CFL condition.
 */
PetscErrorCode IceModel::computeMax2DSlidingSpeed() {
  PetscErrorCode ierr;
  PISMVector2 **v_basal;
  PetscScalar locCFLmaxdt2D = config.get("maximum_time_step_years") * secpera;

  bool do_ocean_kill = config.get_flag("ocean_kill"),
    floating_ice_killed = config.get_flag("floating_ice_killed");

  ierr = vel_basal.get_array(v_basal); CHKERRQ(ierr);
  ierr = vMask.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      // the following conditionals, both -ocean_kill and -float_kill, are also applied in 
      //   IceModel::massContExplicitStep() when zeroing thickness
      const bool ignorableOcean = ( do_ocean_kill && (vMask.value(i,j) == MASK_OCEAN_AT_TIME_0) )
	|| ( floating_ice_killed && vMask.is_floating(i,j) );
      if (!ignorableOcean) {
        PetscScalar denom = PetscAbs(v_basal[i][j].u)/grid.dx + PetscAbs(v_basal[i][j].v)/grid.dy;
        denom += (0.01/secpera)/(grid.dx + grid.dy);  // make sure it's pos.
        locCFLmaxdt2D = PetscMin(locCFLmaxdt2D,1.0/denom);
      }
    }
  }
  ierr = vel_basal.end_access(); CHKERRQ(ierr);
  ierr = vMask.end_access(); CHKERRQ(ierr);

  ierr = PetscGlobalMin(&locCFLmaxdt2D, &CFLmaxdt2D, grid.com); CHKERRQ(ierr);
  return 0;
}


//! Compute the maximum time step allowed by the diffusive SIA.
/*!
Note computeMaxDiffusivity() must be called before this to set \c gDmax.  Note
adapt_ratio * 2 is multiplied by dx^2/(2*maxD) so dt <= adapt_ratio * dx^2/maxD
(if dx=dy)

Reference: \ref MortonMayers pp 62--63.
 */
PetscErrorCode IceModel::adaptTimeStepDiffusivity() {

  bool do_skip = config.get_flag("do_skip");
  
  const PetscScalar adaptTimeStepRatio = config.get("adaptive_timestepping_ratio");

  const PetscInt skip_max = static_cast<PetscInt>(config.get("skip_max"));

  const PetscScalar DEFAULT_ADDED_TO_GDMAX_ADAPT = 1.0e-2;
  const PetscScalar  
          gridfactor = 1.0/(grid.dx*grid.dx) + 1.0/(grid.dy*grid.dy);
  dt_from_diffus = adaptTimeStepRatio
                     * 2 / ((gDmax + DEFAULT_ADDED_TO_GDMAX_ADAPT) * gridfactor);
  if (do_skip && (skipCountDown == 0)) {
    const PetscScalar  conservativeFactor = 0.95;
    // typically "dt" in next line is from CFL for advection in temperature equation,
    //   but in fact it might be from other restrictions, e.g. CFL for mass continuity
    //   in basal sliding case, or max_dt
    skipCountDown = (PetscInt) floor(conservativeFactor * (dt / dt_from_diffus));
    skipCountDown = ( skipCountDown >  skip_max) ?  skip_max :  skipCountDown;
  } // if  skipCountDown > 0 then it will get decremented at the mass balance step
  if (dt_from_diffus < dt) {
    dt = dt_from_diffus;
    adaptReasonFlag = 'd';
  }
  return 0;
}


//! Use various stability criteria to determine the time step for an evolution run.
/*! 
The main loop in run() approximates many physical processes.  Several of these approximations,
including the mass continuity and temperature equations in particular, involve stability
criteria.  This procedure builds the length of the next time step by using these criteria and 
by incorporating choices made by options (e.g. <c>-max_dt</c>) and by derived classes.
 */
PetscErrorCode IceModel::determineTimeStep(const bool doTemperatureCFL) {
  PetscErrorCode ierr;

  bool do_mass_conserve = config.get_flag("do_mass_conserve"),
    do_temp = config.get_flag("do_temp");

  if ( ( (doAdaptTimeStep == PETSC_TRUE) && do_mass_conserve ) ) {
    ierr = computeMaxDiffusivity(view_diffusivity); CHKERRQ(ierr);
  }
  const PetscScalar timeToEnd = (grid.end_year - grid.year) * secpera;
  if (dt_force > 0.0) {
    dt = dt_force; // override usual dt mechanism
    adaptReasonFlag = 'f';
    if (timeToEnd < dt) {
      dt = timeToEnd;
      adaptReasonFlag = 'e';
    }
  } else {
    dt = config.get("maximum_time_step_years") * secpera;
    bool use_ssa_velocity = config.get_flag("use_ssa_velocity");

    adaptReasonFlag = 'm';
    if (doAdaptTimeStep == PETSC_TRUE) {
      if ((do_temp == PETSC_TRUE) && (doTemperatureCFL == PETSC_TRUE)) {
        // CFLmaxdt is set by computeMax3DVelocities() in call to velocity() iMvelocity.cc
        dt_from_cfl = CFLmaxdt;
        if (dt_from_cfl < dt) {
          dt = dt_from_cfl;
          adaptReasonFlag = 'c';
        }
      }
      if (do_mass_conserve && use_ssa_velocity) {
        // CFLmaxdt2D is set by broadcastSSAVelocity()
        if (CFLmaxdt2D < dt) {
          dt = CFLmaxdt2D;
          adaptReasonFlag = 'u';
        }
      }
      if (do_mass_conserve && (computeSIAVelocities == PETSC_TRUE)) {
        // note: if do_skip then skipCountDown = floor(dt_from_cfl/dt_from_diffus)
        ierr = adaptTimeStepDiffusivity(); CHKERRQ(ierr); // might set adaptReasonFlag = 'd'
      }
    }
    if ((maxdt_temporary > 0.0) && (maxdt_temporary < dt)) {
      dt = maxdt_temporary;
      adaptReasonFlag = 't';
    }
    if (timeToEnd < dt) {
      dt = timeToEnd;
      adaptReasonFlag = 'e';
    }
    if ((adaptReasonFlag == 'm') || (adaptReasonFlag == 't') || (adaptReasonFlag == 'e')) {
      if (skipCountDown > 1) skipCountDown = 1; 
    }
  }    
  return 0;
}


//! Because of the -skip mechanism it is still possible that we can have CFL violations: count them.
/*!
This applies to the horizontal part of the three-dimensional advection problem
solved by IceModel::ageStep() and the advection, ice-only part of the problem solved by
temperatureStep().  These methods use a fine vertical grid, and so we consider CFL
violations on that same fine grid. (FIXME: should we actually use the fine grid?)

Communication is needed to determine total CFL violation count over entire grid.
It is handled by temperatureAgeStep(), not here.
*/
PetscErrorCode IceModel::countCFLViolations(PetscScalar* CFLviol) {
  PetscErrorCode  ierr;

  const PetscScalar cflx = grid.dx / dtTempAge,
                    cfly = grid.dy / dtTempAge;

  PetscScalar *u, *v;

  ierr = vH.begin_access(); CHKERRQ(ierr);
  ierr = u3.begin_access(); CHKERRQ(ierr);
  ierr = v3.begin_access(); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscInt  fks = grid.kBelowHeight(vH(i,j));

      ierr = u3.getInternalColumn(i,j,&u); CHKERRQ(ierr);
      ierr = v3.getInternalColumn(i,j,&v); CHKERRQ(ierr);

      // check horizontal CFL conditions at each point
      for (PetscInt k=0; k<=fks; k++) {
        if (PetscAbs(u[k]) > cflx)  *CFLviol += 1.0;
        if (PetscAbs(v[k]) > cfly)  *CFLviol += 1.0;
      }
    }
  }

  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = u3.end_access();  CHKERRQ(ierr);
  ierr = v3.end_access();  CHKERRQ(ierr);

  return 0;
}

