// Copyright (C) 2004--2009 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <cstring>
#include <cmath>
#include "iceModel.hh"

//! Read runtime (command line) options and alter the corresponding parameters or flags as appropriate.
/*!

A critical principle of this procedure is that it will not alter IceModel parameters and flags
\e unless the user sets an option to do so.  Thus this base class setFromOptions() can be
called by a derived class after the derived class has set its own defaults for base class
parameters and flags.

Also, this procedure should not allocate memory or create new objects using the
new operator.

In fact this procedure only reads the majority of the options.  Some are read in 
initFromOptions(), writeFiles(), and setStartRunEndYearsFromOptions(), among other places.

Note there are no options to directly set \c dx, \c dy, \c dz, \c Lbz, and \c year as the user 
should not directly set these grid parameters.  There are, however, options for directly 
setting \c Mx, \c My, \c Mz, \c Mbz and also \c Lx, \c Ly, \c Lz.

Note that additional options are read by PISMClimateCoupler instances, including -pdd... 
and -d?forcing options.
 */
PetscErrorCode  IceModel::setFromOptions() {
  PetscErrorCode ierr;
  PetscTruth  my_useConstantNuH, mybedDeflc, mydoBedIso, 
              myincludeBMRinContinuity, mydoOceanKill, floatkillSet,
              mydoPlasticTill, myuseSSAVelocity, myssaSystemToASCIIMatlab,
              pseudoplasticSet, pseudoplasticqSet, pseudoplasticuthresholdSet,
              mydoSuperpose, mydoSkip, plasticRegSet,
              plasticc0Set, plasticphiSet, myholdTillYieldStress, realageSet,
              maxdtSet, noMassConserve, noTemp, etaSet, doShelvesDragToo,
              mygsConstantSet;
  PetscScalar my_maxdt, my_nuH,
              myplastic_till_c_0, myplastic_phi, myPlasticRegularization,
              mypseudo_plastic_q, mypseudo_plastic_uthreshold;

  ierr = verbPrintf(3, grid.com,
		    "Processing physics-related command-line options...\n"); CHKERRQ(ierr);

  // OptionsBegin/End probably has no effect for now, but perhaps some day PETSc will show a GUI which
  // allows users to set options using this.
  ierr = PetscOptionsBegin(grid.com,PETSC_NULL,"IceModel options (PISM)",PETSC_NULL); 
          CHKERRQ(ierr);

  /* 
  note on pass-by-reference for options:
     For the last argument "flag" to PetscOptionsXXXX(....,&flag), the flag always indicates
     whether the option has been set.  Therefore "flag" is altered by this function call.
     For other arguments "value" to PetscOptionsXXXX(....,&value,&flag), the value of "value"
     is only set if the user specified the option.  Therefore "flag" should always be given a
     local PetscTruth variable if we want to preserve previously set IceModel flags.  By 
     contrast, for various parameters "value" we can use the IceModel parameter itself
     without fear of overwriting defaults unless, of course, the user wants them overwritten.
     It is also o.k. to have a local variable for "value", and then proceed to set the IceModel
     member accordingly.
  */

  ierr = PetscOptionsGetReal(PETSC_NULL, "-adapt_ratio", &adaptTimeStepRatio,
                               PETSC_NULL); CHKERRQ(ierr);

  ierr = check_option("-bed_def_iso", mydoBedIso); CHKERRQ(ierr);
  if (mydoBedIso == PETSC_TRUE) {
    doBedDef = PETSC_TRUE;
    doBedIso = PETSC_TRUE;
  }

  ierr = check_option("-bed_def_lc", mybedDeflc); CHKERRQ(ierr);  
  if (mybedDeflc == PETSC_TRUE) {
    doBedDef = PETSC_TRUE;
    doBedIso = PETSC_FALSE;
  }

  if ((mydoBedIso == PETSC_TRUE) && (mybedDeflc == PETSC_TRUE))  {
    ierr = verbPrintf(1,grid.com,
       "WARNING: both options -bed_def_iso and -bed_def_lc set; using Lingle & Clark model\n");
       CHKERRQ(ierr);
  }

  ierr = check_option("-bmr_in_cont", myincludeBMRinContinuity);
      CHKERRQ(ierr);
  if (myincludeBMRinContinuity == PETSC_TRUE)   includeBMRinContinuity = PETSC_TRUE;

// "-cbar_to_till" read in invertVelocitiesFromNetCDF() in iMinverse.cc

// "-chebZ" read in determineSpacingTypeFromOptions()

  ierr = PetscOptionsGetReal(PETSC_NULL, "-constant_nuH", &my_nuH, &my_useConstantNuH); CHKERRQ(ierr);
  // user gives nu*H in MPa yr m (e.g. Ritz et al 2001 value is 30.0 * 1.0)
  if (my_useConstantNuH == PETSC_TRUE) {
    setConstantNuHForSSA(my_nuH  * 1.0e6 * secpera); // convert to Pa s m
  }

// "-csurf_to_till" read in invertVelocitiesFromNetCDF() in iMinverse.cc

  // regular size viewers
  ierr = PetscOptionsGetString(PETSC_NULL, "-d", diagnostic, PETSC_MAX_PATH_LEN, PETSC_NULL); 
            CHKERRQ(ierr);
  if (showViewers == PETSC_FALSE) {
    ierr = verbPrintf(1,grid.com,
       "WARNING: viewers requested with -d but showViewers is false, so none shown\n");
       CHKERRQ(ierr);
    strcpy(diagnostic, "\0");
  }
  
  // big viewers (which have higher priority than regular viewers)
  ierr = PetscOptionsGetString(PETSC_NULL, "-dbig", diagnosticBIG, PETSC_MAX_PATH_LEN, PETSC_NULL); 
            CHKERRQ(ierr);
  if (showViewers == PETSC_FALSE)  {
    ierr = verbPrintf(1,grid.com,
       "WARNING: viewers requested with -dbig but showViewers is false, so none shown\n");
       CHKERRQ(ierr);
    strcpy(diagnosticBIG, "\0");
  }

  ierr = PetscOptionsGetReal(PETSC_NULL, "-e", &enhancementFactor, PETSC_NULL); CHKERRQ(ierr);

  ierr = check_option("-eta", etaSet); CHKERRQ(ierr);
  if (etaSet == PETSC_TRUE)  transformForSurfaceGradient = PETSC_TRUE;

// note "-f3d" is read in writefiles() in iMIO.cc

  // whether or not to kill ice (zero thickness) if it is (or becomes) floating
  ierr = check_option("-float_kill", floatkillSet); CHKERRQ(ierr);
  if (floatkillSet == PETSC_TRUE)  floatingIceKilled = PETSC_TRUE;

  // note "-gk" is used for specifying Goldsby-Kohlstedt ice
  //   this form allows a constant value of grain size to be input in mm
  ierr = PetscOptionsGetReal(PETSC_NULL, "-gk", &constantGrainSize, &mygsConstantSet); CHKERRQ(ierr);
  if (mygsConstantSet == PETSC_TRUE)  {
    constantGrainSize *= 1.0e-3;
    ierr = iceFactory.setType(ICE_HYBRID);CHKERRQ(ierr);
  }

  // note "-gk_age" is also used for specifying Goldsby-Kohlstedt ice;
  ierr = check_option("-gk_age", realAgeForGrainSize); CHKERRQ(ierr);
  if (realAgeForGrainSize) {
    ierr = iceFactory.setType(ICE_HYBRID);CHKERRQ(ierr);
  }

  ierr = check_option("-hold_tauc", myholdTillYieldStress); CHKERRQ(ierr);
  if (myholdTillYieldStress == PETSC_TRUE)    holdTillYieldStress = PETSC_TRUE;

  ierr = PetscOptionsGetInt(PETSC_NULL, "-id", &id, PETSC_NULL); CHKERRQ(ierr);

// note "-i" is in use for input file name; see initFromOptions() in iMutil.cc

  ierr = PetscOptionsGetInt(PETSC_NULL, "-jd", &jd, PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsGetInt(PETSC_NULL, "-kd", &kd, PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsGetReal(PETSC_NULL, "-low_temp", &globalMinAllowedTemp, 
           PETSC_NULL); CHKERRQ(ierr);

// note "-mato" caught in writeFiles() in iMIO.cc

// note "-matv" caught in writeFiles() in iMIO.cc

  ierr = PetscOptionsGetReal(PETSC_NULL, "-max_dt", &my_maxdt, &maxdtSet); CHKERRQ(ierr);
  if (maxdtSet == PETSC_TRUE)    setMaxTimeStepYears(my_maxdt);

  ierr = PetscOptionsGetInt(PETSC_NULL, "-max_low_temps", &maxLowTempCount, PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsGetReal(PETSC_NULL, "-mu_sliding", &muSliding, PETSC_NULL); CHKERRQ(ierr);

  ierr = check_option("-no_mass", noMassConserve); CHKERRQ(ierr);
  if (noMassConserve == PETSC_TRUE)    doMassConserve = PETSC_FALSE;

/* REMOVED TO AVOID STENCIL_BOX COMMUNICATION FOR 3D Vecs: 
  // -no_spokes K for K=0,1,2,... turns on smoothing of spokes by smoothing Sigma 
  // (e.g. in EISMINT experiment F) values K>3 not recommended (lots of communication!)
  ierr = PetscOptionsGetInt(PETSC_NULL, "-no_spokes", &noSpokesLevel, PETSC_NULL); CHKERRQ(ierr);
*/

  ierr = check_option("-no_temp", noTemp); CHKERRQ(ierr);
  if (noTemp == PETSC_TRUE)   doTemp = PETSC_FALSE;

// note "-o" is in use for output file name; see iMIO.cc

  // whether or not to kill ice at locations where mask=FLOATING_OCEAN0;
  //   also determines if mask=FLOATING_OCEAN0 or mask=FLOATING
  //   at bootstrapping (-boot_from), if original condition was ice-free ocean
  ierr = check_option("-ocean_kill", mydoOceanKill); CHKERRQ(ierr);
  if (mydoOceanKill == PETSC_TRUE)   doOceanKill = PETSC_TRUE;

  // use a plastic basal till mechanical model
  ierr = check_option("-plastic", mydoPlasticTill); CHKERRQ(ierr);
  if (mydoPlasticTill == PETSC_TRUE)   doPlasticTill = PETSC_TRUE;

  // plastic_till_c_0 is a parameter in the computation of the till yield stress tau_c
  // from the thickness of the basal melt water; see updateYieldStressFromHmelt()
  // option given in kPa so convert to Pa
  ierr = PetscOptionsGetReal(PETSC_NULL, "-plastic_c0", &myplastic_till_c_0, 
     &plasticc0Set);  CHKERRQ(ierr);
  if (plasticc0Set == PETSC_TRUE)   plastic_till_c_0 = myplastic_till_c_0 * 1.0e3;

  // plastic_till_pw_fraction is a parameter in the computation of the till yield stress tau_c
  // from the thickness of the basal melt water; see updateYieldStressFromHmelt()
  // option a pure number (a fraction); no conversion
  ierr = PetscOptionsGetReal(PETSC_NULL, "-plastic_pwfrac", &plastic_till_pw_fraction, 
     PETSC_NULL);  CHKERRQ(ierr);

  // controls regularization of plastic basal sliding law
  // option given in m/a so convert to m/s
  ierr = PetscOptionsGetReal(PETSC_NULL, "-plastic_reg", &myPlasticRegularization, 
     &plasticRegSet);  CHKERRQ(ierr);
  if (plasticRegSet == PETSC_TRUE)  plasticRegularization = myPlasticRegularization / secpera;

  // plastic_till_mu is a parameter in the computation of the till yield stress tau_c
  // from the thickness of the basal melt water; see updateYieldStressFromHmelt()
  // option in degrees is the "friction angle"
  ierr = PetscOptionsGetReal(PETSC_NULL, "-plastic_phi", &myplastic_phi, 
     &plasticphiSet);  CHKERRQ(ierr);
  if (plasticphiSet == PETSC_TRUE)
     plastic_till_mu = tan((pi/180.0) * myplastic_phi);

  // use pseudo plastic instead of pure plastic; see iMbasal.cc
  ierr = check_option("-pseudo_plastic", pseudoplasticSet);  CHKERRQ(ierr);
  if (pseudoplasticSet == PETSC_TRUE) {
     doPseudoPlasticTill = PETSC_TRUE;
  }

  // power in denominator on pseudo_plastic_uthreshold; typical is q=0.25; q=0 is pure plastic
  ierr = PetscOptionsGetReal(PETSC_NULL, "-pseudo_plastic_q", &mypseudo_plastic_q, 
     &pseudoplasticqSet);  CHKERRQ(ierr);
  if (pseudoplasticqSet == PETSC_TRUE) {
     doPseudoPlasticTill = PETSC_TRUE;
     pseudo_plastic_q = mypseudo_plastic_q;
  }

  // threshold; at this velocity tau_c is basal shear stress
  ierr = PetscOptionsGetReal(PETSC_NULL, "-pseudo_plastic_uthreshold", 
     &mypseudo_plastic_uthreshold, &pseudoplasticuthresholdSet);  CHKERRQ(ierr);
  if (pseudoplasticuthresholdSet == PETSC_TRUE) {
     doPseudoPlasticTill = PETSC_TRUE;
     pseudo_plastic_uthreshold = mypseudo_plastic_uthreshold;
  }

  // see updateGrainSizeNow(); option to choose modeled age vtau instead of pseudo age in
  // computing grainsize through Vostok core correlation
  ierr = check_option("-real_age_grainsize", realageSet); CHKERRQ(ierr);
  //if (realageSet == PETSC_TRUE)   realAgeForGrainSize = PETSC_TRUE;
  if (realageSet == PETSC_TRUE) {
    ierr = PetscPrintf(grid.com,
		       "PISM ERROR: -real_age_grainsize (sets realAgeForGrainSize) not implemented\n");
    CHKERRQ(ierr);
    PetscEnd();
  }

// note "-regrid_from" is in use for regrid file name; see iMregrid.cc

// note "-regrid_vars" is in use for regrid variable names; see iMregrid.cc

  // see assembleSSAMatrix(); used in MISMIP
  ierr = check_option("-shelves_drag_too", doShelvesDragToo); CHKERRQ(ierr);
  if (doShelvesDragToo == PETSC_TRUE)   shelvesDragToo = PETSC_TRUE;
  
  ierr = check_option("-ssa", myuseSSAVelocity); CHKERRQ(ierr);
  if (myuseSSAVelocity == PETSC_TRUE)   useSSAVelocity = PETSC_TRUE;
  
  ierr = PetscOptionsGetReal(PETSC_NULL, "-ssa_eps", &ssaEpsilon, PETSC_NULL); CHKERRQ(ierr);
  
  // option to save linear system in Matlab-readable ASCII format at end of each
  // numerical solution of SSA equations; can be given with or without filename prefix
  // (i.e. "-ssa_matlab " or "-ssa_matlab foo" are both legal; in former case get 
  // "pism_SSA_[year].m" if "pism_SSA" is default prefix, and in latter case get "foo_[year].m")
  ierr = check_option("-ssa_matlab", myssaSystemToASCIIMatlab); CHKERRQ(ierr);
  if (myssaSystemToASCIIMatlab == PETSC_TRUE)   ssaSystemToASCIIMatlab = PETSC_TRUE;
  if (ssaSystemToASCIIMatlab == PETSC_TRUE) {  // now get the prefix if it was given by user
    char tempPrefix[PETSC_MAX_PATH_LEN];
    ierr = PetscOptionsGetString(PETSC_NULL, "-ssa_matlab", tempPrefix, 
             PETSC_MAX_PATH_LEN, PETSC_NULL); CHKERRQ(ierr);
    if (strlen(tempPrefix) > 0) {
      strcpy(ssaMatlabFilePrefix, tempPrefix);
    } // otherwise keep default prefix, whatever it was
  }

  ierr = PetscOptionsGetInt(PETSC_NULL, "-ssa_maxi", &ssaMaxIterations,
           PETSC_NULL); CHKERRQ(ierr);
           
  ierr = PetscOptionsGetReal(PETSC_NULL, "-ssa_rtol", &ssaRelativeTolerance,
           PETSC_NULL); CHKERRQ(ierr);

// -ssaBC used in IceROSSModel
  
  // apply "glaciological superposition to low order", i.e. add SIA results to those of 
  // SSA equations where DRAGGING; this version is  U = f(|v|) u + v   where u is SIA and v is SSA
  ierr = check_option("-super", mydoSuperpose); CHKERRQ(ierr);
  if (mydoSuperpose == PETSC_TRUE) {
    doSuperpose = PETSC_TRUE;
  }

  /* This controls allows more than one mass continuity steps per temperature/age and
     SSA computation */
  ierr = PetscOptionsGetInt(PETSC_NULL, "-skip", &skipMax, &mydoSkip); CHKERRQ(ierr);
  if (mydoSkip == PETSC_TRUE)   doSkip = PETSC_TRUE;

  // Process -y, -ys, -ye. We are reading these options here because couplers
  // might need to know what year it is.
  ierr = set_time_from_options();

  ierr = PetscOptionsEnd(); CHKERRQ(ierr);
  return 0;
}
