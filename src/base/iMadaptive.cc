// Copyright (C) 2004-2014 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include "iceModel.hh"
#include <petscvec.h>
#include "Mask.hh"
#include "PISMStressBalance.hh"
#include "bedrockThermalUnit.hh"
#include "PISMTime.hh"
#include "PISMEigenCalving.hh"

//! Compute the maximum velocities for time-stepping and reporting to user.
/*!
Computes the maximum magnitude of the components \f$u,v,w\f$ of the 3D velocity.
Then sets `CFLmaxdt`, the maximum time step allowed under the
Courant-Friedrichs-Lewy (CFL) condition on the
horizontal advection scheme for age and for temperature.

Under BOMBPROOF there is no CFL condition for the vertical advection.
The maximum vertical velocity is computed but it does not affect
`CFLmaxdt`.
 */
PetscErrorCode IceModel::computeMax3DVelocities() {
  PetscErrorCode ierr;
  double *u, *v, *w;
  double locCFLmaxdt = config.get("maximum_time_step_years", "years", "seconds");

  IceModelVec3 *u3, *v3, *w3;

  MaskQuery mask(vMask);

  ierr = stress_balance->get_3d_velocity(u3, v3, w3); CHKERRQ(ierr);

  ierr = ice_thickness.begin_access(); CHKERRQ(ierr);
  ierr = u3->begin_access(); CHKERRQ(ierr);
  ierr = v3->begin_access(); CHKERRQ(ierr);
  ierr = w3->begin_access(); CHKERRQ(ierr);
  ierr = vMask.begin_access(); CHKERRQ(ierr);

  // update global max of abs of velocities for CFL; only velocities under surface
  double   maxu=0.0, maxv=0.0, maxw=0.0;
  for (int i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (int j = grid.ys; j < grid.ys + grid.ym; ++j) {
      if (mask.icy(i, j)) {
        const int ks = grid.kBelowHeight(ice_thickness(i, j));
        ierr = u3->getInternalColumn(i, j, &u); CHKERRQ(ierr);
        ierr = v3->getInternalColumn(i, j, &v); CHKERRQ(ierr);
        ierr = w3->getInternalColumn(i, j, &w); CHKERRQ(ierr);
        for (int k = 0; k <= ks; ++k) {
          const double absu = PetscAbs(u[k]),
                          absv = PetscAbs(v[k]);
          maxu = PetscMax(maxu, absu);
          maxv = PetscMax(maxv, absv);
          maxw = PetscMax(maxw, PetscAbs(w[k]));
          const double denom = PetscAbs(absu / grid.dx) + PetscAbs(absv / grid.dy);
          if (denom > 0.0)
            locCFLmaxdt = PetscMin(locCFLmaxdt, 1.0 / denom);
        }
      }
    }
  }

  ierr = vMask.end_access(); CHKERRQ(ierr);
  ierr = u3->end_access(); CHKERRQ(ierr);
  ierr = v3->end_access(); CHKERRQ(ierr);
  ierr = w3->end_access(); CHKERRQ(ierr);
  ierr = ice_thickness.end_access(); CHKERRQ(ierr);

  ierr = PISMGlobalMax(&maxu, &gmaxu, grid.com); CHKERRQ(ierr);
  ierr = PISMGlobalMax(&maxv, &gmaxv, grid.com); CHKERRQ(ierr);
  ierr = PISMGlobalMax(&maxw, &gmaxw, grid.com); CHKERRQ(ierr);
  ierr = PISMGlobalMin(&locCFLmaxdt, &CFLmaxdt, grid.com); CHKERRQ(ierr);
  return 0;
}


//! Compute the CFL constant associated to first-order upwinding for the sliding contribution to mass continuity.
/*!
  This procedure computes the maximum horizontal speed in the icy
  areas. In particular it computes CFL constant for the upwinding, in
  massContExplicitStep(), which applies to the basal component of mass
  flux.

  That is, because the map-plane mass continuity is advective in the
  sliding case we have a CFL condition.
 */
PetscErrorCode IceModel::computeMax2DSlidingSpeed() {
  PetscErrorCode ierr;
  double locCFLmaxdt2D = config.get("maximum_time_step_years", "years", "seconds");

  MaskQuery mask(vMask);

  IceModelVec2V *vel_advective;
  ierr = stress_balance->get_2D_advective_velocity(vel_advective); CHKERRQ(ierr);
  IceModelVec2V &vel = *vel_advective; // a shortcut

  ierr = vel.begin_access(); CHKERRQ(ierr);
  ierr = vMask.begin_access(); CHKERRQ(ierr);
  for (int i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (int j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (mask.icy(i, j)) {
        const double denom = PetscAbs(vel(i,j).u)/grid.dx + PetscAbs(vel(i,j).v)/grid.dy;
        if (denom > 0.0)
          locCFLmaxdt2D = PetscMin(locCFLmaxdt2D, 1.0/denom);
      }
    }
  }
  ierr = vel.end_access(); CHKERRQ(ierr);
  ierr = vMask.end_access(); CHKERRQ(ierr);

  ierr = PISMGlobalMin(&locCFLmaxdt2D, &CFLmaxdt2D, grid.com); CHKERRQ(ierr);
  return 0;
}


//! Compute the maximum time step allowed by the diffusive SIA.
/*!
Updates gDmax.  If gDmax is positive (i.e. if there is diffusion going on) then
updates dt.  Updates skipCountDown if it is zero.

Note adapt_ratio * 2 is multiplied by dx^2/(2*maxD) so dt <= adapt_ratio *
dx^2/maxD (if dx=dy).

Reference: [\ref MortonMayers] pp 62--63.
 */
PetscErrorCode IceModel::adaptTimeStepDiffusivity() {
  PetscErrorCode ierr;

  ierr = stress_balance->get_max_diffusivity(gDmax); CHKERRQ(ierr);

  double dt_from_diffus = -1.0;
  if (gDmax > 0.0) {
    const double adaptTimeStepRatio = config.get("adaptive_timestepping_ratio");
    const double
          gridfactor = 1.0/(grid.dx*grid.dx) + 1.0/(grid.dy*grid.dy);
    dt_from_diffus = adaptTimeStepRatio * 2 / (gDmax * gridfactor);
  }

  if (config.get_flag("do_skip") && skipCountDown == 0) {
    const int skip_max = static_cast<int>(config.get("skip_max"));
    if (dt_from_diffus > 0.0) {
      const double  conservativeFactor = 0.95;
      // typically "dt" in next line is from CFL for advection in temperature equation,
      //   but in fact it might be from other restrictions, e.g. CFL for mass continuity
      //   in basal sliding case, or max_dt
      skipCountDown = (int) floor(conservativeFactor * (dt / dt_from_diffus));
      skipCountDown = ( skipCountDown >  skip_max) ?  skip_max :  skipCountDown;
    } else
      skipCountDown = skip_max;
  }

  if (dt_from_diffus > 0.0 && dt_from_diffus < dt) {
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
    do_energy = config.get_flag("do_energy");

  const double timeToEnd = grid.time->end() - grid.time->current();
  if (dt_force > 0.0) {
    dt = dt_force; // override usual dt mechanism
    adaptReasonFlag = 'f';
    if (timeToEnd < dt) {
      dt = timeToEnd;
      adaptReasonFlag = 'e';
    }
  } else {
    dt = config.get("maximum_time_step_years", "years", "seconds");

    adaptReasonFlag = 'm';

    if ((do_energy == PETSC_TRUE) && (doTemperatureCFL == PETSC_TRUE)) {
      // CFLmaxdt is set by computeMax3DVelocities() in call to velocity() iMvelocity.cc
      dt_from_cfl = CFLmaxdt;
      if (dt_from_cfl < dt) {
        dt = dt_from_cfl;
        adaptReasonFlag = 'c';
      }
    }
    if (btu) {
      double btu_dt;
      bool restrict;
      ierr = btu->max_timestep(grid.time->current(),
                               btu_dt, restrict); CHKERRQ(ierr);
      if (restrict && btu_dt < dt) {
        dt = btu_dt;
        adaptReasonFlag = 'b';
      }
    }
    if (do_mass_conserve) {
      // CFLmaxdt2D is set by IceModel::computeMax2DSlidingSpeed()
      if (CFLmaxdt2D < dt) {
        dt = CFLmaxdt2D;
        adaptReasonFlag = 'u';
      }
    }
    if (do_mass_conserve) {
      // note: also sets skipCountDown;  if skipCountDown > 0 then it will get
      // decremented at the mass balance step
      ierr = adaptTimeStepDiffusivity(); CHKERRQ(ierr); // might set adaptReasonFlag = 'd'
    }

    if (eigen_calving != NULL) {
      bool restrict;
      double dt_from_eigencalving;
      ierr = eigen_calving->max_timestep(grid.time->current(),
                                         dt_from_eigencalving, restrict); CHKERRQ(ierr);
      if (restrict == true && dt_from_eigencalving < dt) {
        dt = dt_from_eigencalving;
        adaptReasonFlag = 'k';
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
PetscErrorCode IceModel::countCFLViolations(double* CFLviol) {
  PetscErrorCode  ierr;

  const double cflx = grid.dx / dt_TempAge,
                    cfly = grid.dy / dt_TempAge;

  double *u, *v;
  IceModelVec3 *u3, *v3, *dummy;
  ierr = stress_balance->get_3d_velocity(u3, v3, dummy); CHKERRQ(ierr);

  ierr = ice_thickness.begin_access(); CHKERRQ(ierr);
  ierr = u3->begin_access(); CHKERRQ(ierr);
  ierr = v3->begin_access(); CHKERRQ(ierr);

  for (int i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (int j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const int  fks = grid.kBelowHeight(ice_thickness(i,j));

      ierr = u3->getInternalColumn(i,j,&u); CHKERRQ(ierr);
      ierr = v3->getInternalColumn(i,j,&v); CHKERRQ(ierr);

      // check horizontal CFL conditions at each point
      for (int k=0; k<=fks; k++) {
        if (PetscAbs(u[k]) > cflx)  *CFLviol += 1.0;
        if (PetscAbs(v[k]) > cfly)  *CFLviol += 1.0;
      }
    }
  }

  ierr = ice_thickness.end_access(); CHKERRQ(ierr);
  ierr = u3->end_access();  CHKERRQ(ierr);
  ierr = v3->end_access();  CHKERRQ(ierr);

  return 0;
}

