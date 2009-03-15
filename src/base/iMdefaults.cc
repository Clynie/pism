// Copyright (C) 2004-2009 Jed Brown and Ed Bueler
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


const PetscScalar DEFAULT_START_YEAR = 0;
const PetscScalar DEFAULT_RUN_YEARS = 1000.0;  // years

const PetscScalar DEFAULT_ADAPT_TIMESTEP_RATIO = 0.12;  // yes, I'm confident this is o.k.

const PetscScalar DEFAULT_EPSILON_SSA = 1.0e15;  // kg m^-1 s^-1;
         // initial amount of (denominator) regularization in computation of effective viscosity
const PetscScalar DEFAULT_TAUC = 1e4;  // 10^4 Pa = 0.1 bar
         // used in iMvelocity.C and iMutil.C
const PetscScalar DEFAULT_MIN_TEMP_FOR_SLIDING = 273.0;  // note less than 
         // ice.meltingTemp; if above this value then decide to slide
const PetscScalar DEFAULT_INITIAL_AGE_YEARS = 0.0;  // age to start age computation

const PetscScalar DEFAULT_MAX_HMELT = 2.0;  // max of 2 m thick basal melt water layer

const PetscScalar DEFAULT_MAX_TIME_STEP_YEARS = 60.0;  // years

const PetscScalar DEFAULT_ENHANCEMENT_FACTOR = 1.0;
const PetscTruth  DEFAULT_DO_MASS_CONSERVE = PETSC_TRUE;
const PetscTruth  DEFAULT_DO_TEMP = PETSC_TRUE;
const PetscTruth  DEFAULT_DO_SKIP = PETSC_FALSE; // off by default
const PetscInt    DEFAULT_SKIP_MAX = 10;
const PetscScalar DEFAULT_GLOBAL_MIN_ALLOWED_TEMP = 200.0;
const PetscInt    DEFAULT_MAX_LOW_TEMP_COUNT = 10;  // 

const PetscScalar DEFAULT_CONSTANT_GRAIN_SIZE = 1.0e-3;  // 1 mm

const PetscTruth  DEFAULT_INCLUDE_BMR_IN_CONTINUITY = PETSC_TRUE;
const PetscTruth  DEFAULT_THERMAL_BEDROCK = PETSC_TRUE;
const PetscInt    DEFAULT_NOSPOKESLEVEL = 0;  // iterations of smoothing of Sigma
//const PetscScalar DEFAULT_MU_SLIDING = 3.17e-11;  // 100 m/a at 100kPa
const PetscScalar DEFAULT_MU_SLIDING = 0.0;

const PetscTruth  DEFAULT_DO_BED_DEF = PETSC_FALSE;
const PetscTruth  DEFAULT_DO_BED_ISO = PETSC_FALSE;
// model is so cheap you might as well update frequently:
const PetscScalar DEFAULT_BED_DEF_INTERVAL_YEARS = 10.0;  

const PetscTruth  DEFAULT_IS_DRY_SIMULATION = PETSC_FALSE;
const PetscTruth  DEFAULT_OCEAN_KILL = PETSC_FALSE;
const PetscTruth  DEFAULT_FLOATING_ICE_KILLED = PETSC_FALSE;

const PetscTruth  DEFAULT_USE_SSA_VELOCITY = PETSC_FALSE;
const PetscTruth  DEFAULT_DO_PLASTIC_TILL = PETSC_FALSE;
const PetscTruth  DEFAULT_DO_SUPERPOSE = PETSC_FALSE;
const PetscInt    DEFAULT_MAX_ITERATIONS_SSA = 300;
const PetscTruth  DEFAULT_USE_CONSTANT_NUH_FOR_SSA = PETSC_FALSE;
const PetscTruth  DEFAULT_COMPUTE_SURF_GRAD_INWARD_SSA = PETSC_FALSE;

// for next constants, note (VELOCITY/LENGTH)^2  is very close to 10^-27; compare "\epsilon^2/L^2" which
// appears in formula (4.1) in C. Schoof 2006 "A variational approach to ice streams" J Fluid Mech 556 pp 227--251
const PetscScalar DEFAULT_PLASTIC_REGULARIZATION = 0.01 / secpera;
const PetscTruth  DEFAULT_DO_PSEUDO_PLASTIC_TILL = PETSC_FALSE;
const PetscScalar DEFAULT_PSEUDO_PLASTIC_Q = 0.25;  // see PlasticBasalType
const PetscScalar DEFAULT_PSEUDO_PLASTIC_UTHRESHOLD = 100 / secpera; // 100 m/a
const PetscScalar DEFAULT_SSA_RELATIVE_CONVERGENCE = 1.0e-4;

const PetscScalar DEFAULT_BETA_SHELVES_DRAG_TOO = 1.8e9 * 0.0001;  // Pa s m^{-1}
                             //  (1/10000) of value stated in Hulbe&MacAyeal1999 for ice stream E

// pure number; pore water pressure is this fraction of overburden:
const PetscScalar DEFAULT_TILL_PW_FRACTION = 0.95;  
const PetscScalar DEFAULT_TILL_C_0 = 0.0;  // Pa; cohesion of till; 
            // note Schoof uses zero but Paterson pp 168--169 gives range 0 -- 40 kPa; but Paterson
            // notes that "... all the pairs c_0 and phi in the table would give a yield stress
            // for Ice Stream B that exceeds the basal shear stress there ..."
const PetscScalar DEFAULT_TILL_PHI = 30.0;  // pure number; tan(30^o) = ; till friction angle


//! Assigns default values to the many parameters and flags in IceModel.
/*!
The order of precedence for setting parameters in PISM is:
  - default values: Reasonable values to set up the model with are given in setDefaults()
    and in file pism/src/base/iMdefaults.  setDefaults() is called in the constructor for
    IceModel.  It would be reasonable to have setDefaults() read the defaults from a
    (default!) NetCDF file of a format so that others could be substituted.
  - derived class overrides:  The constructor of a derived class can choose its own 
    defaults for data members of IceModel (and its own data members).  These will override
    the above.
  - command line options:  The driver calls IceModel::setFromOptions() after the instance of
    IceModel (or a derived class) is constructed.  setFromOptions() is virtual but should 
    usually be called first if a derived class has a setFromOptions.

The input file (\c -i or \c -boot_from) will not contain (in Feb 2008 version of PISM) any values 
for the quantities which are set in setDefaults().  (There are parameters which can be set at
the command line or by the input file, like \c grid.Mx.  For \c -i the data file has the final
word but for -boot_from the command line options have the final word.)
 
The defaults should be reasonable values under all circumstances or they should indicate 
missing values in some manner.
 */
PetscErrorCode IceModel::setDefaults() {
  PetscErrorCode ierr;
  
  ierr = verbPrintf(3,grid.com, "setting IceModel defaults...\n"); CHKERRQ(ierr);

  // No X11 diagnostics by default, but allow them
  strcpy(diagnostic, "");
  strcpy(diagnosticBIG, "");
  showViewers = PETSC_TRUE;

  ierr = setExecName("pism"); CHKERRQ(ierr);  // drivers typically override this

  enhancementFactor = DEFAULT_ENHANCEMENT_FACTOR;
  muSliding = DEFAULT_MU_SLIDING;
  thermalBedrock = DEFAULT_THERMAL_BEDROCK;
  doOceanKill = DEFAULT_OCEAN_KILL;
  floatingIceKilled = DEFAULT_FLOATING_ICE_KILLED;

  grid.vertical_spacing = EQUAL;
  
  computeSIAVelocities = PETSC_TRUE;
  transformForSurfaceGradient = PETSC_FALSE;

  useSSAVelocity = DEFAULT_USE_SSA_VELOCITY;
  doPlasticTill = DEFAULT_DO_PLASTIC_TILL;
  doPseudoPlasticTill = DEFAULT_DO_PSEUDO_PLASTIC_TILL;
  doSuperpose = DEFAULT_DO_SUPERPOSE;
  ssaMaxIterations = DEFAULT_MAX_ITERATIONS_SSA;
  useConstantNuHForSSA = DEFAULT_USE_CONSTANT_NUH_FOR_SSA;
  ssaRelativeTolerance = DEFAULT_SSA_RELATIVE_CONVERGENCE;
  ssaEpsilon = DEFAULT_EPSILON_SSA;
  computeSurfGradInwardSSA = DEFAULT_COMPUTE_SURF_GRAD_INWARD_SSA;
  ssaSystemToASCIIMatlab = PETSC_FALSE;
  leaveNuHAloneSSA = PETSC_FALSE;
  strcpy(ssaMatlabFilePrefix, "pism_SSA");

  plastic_till_pw_fraction = DEFAULT_TILL_PW_FRACTION;
  plastic_till_c_0 = DEFAULT_TILL_C_0;
  plastic_till_mu = tan((pi/180.0)*DEFAULT_TILL_PHI);
  plasticRegularization = DEFAULT_PLASTIC_REGULARIZATION;
  tauc_default_value = DEFAULT_TAUC;
  pseudo_plastic_q = DEFAULT_PSEUDO_PLASTIC_Q;
  pseudo_plastic_uthreshold = DEFAULT_PSEUDO_PLASTIC_UTHRESHOLD;
  holdTillYieldStress = PETSC_FALSE;
  useConstantTillPhi = PETSC_FALSE;
  
  shelvesDragToo = PETSC_FALSE;
  betaShelvesDragToo = DEFAULT_BETA_SHELVES_DRAG_TOO;
  
  Hmelt_max = DEFAULT_MAX_HMELT;

  setMaxTimeStepYears(DEFAULT_MAX_TIME_STEP_YEARS);
  setAdaptTimeStepRatio(DEFAULT_ADAPT_TIMESTEP_RATIO);
  setAllGMaxVelocities(-1.0);

  run_year_default = DEFAULT_RUN_YEARS;
  setStartYear(DEFAULT_START_YEAR);
  setEndYear(DEFAULT_RUN_YEARS + DEFAULT_START_YEAR);
  yearsStartRunEndDetermined = PETSC_FALSE;
  initial_age_years_default = DEFAULT_INITIAL_AGE_YEARS;

  doMassConserve = DEFAULT_DO_MASS_CONSERVE;
  doTemp = DEFAULT_DO_TEMP;
  doSkip = DEFAULT_DO_SKIP;
  skipMax = DEFAULT_SKIP_MAX;
  reportHomolTemps = PETSC_TRUE;
  globalMinAllowedTemp = DEFAULT_GLOBAL_MIN_ALLOWED_TEMP;
  maxLowTempCount = DEFAULT_MAX_LOW_TEMP_COUNT;
  min_temperature_for_SIA_sliding = DEFAULT_MIN_TEMP_FOR_SLIDING;  
  includeBMRinContinuity = DEFAULT_INCLUDE_BMR_IN_CONTINUITY;

  isDrySimulation = DEFAULT_IS_DRY_SIMULATION;
  
  updateHmelt = PETSC_TRUE;

  realAgeForGrainSize = PETSC_FALSE;
  constantGrainSize = DEFAULT_CONSTANT_GRAIN_SIZE;

  doBedDef = DEFAULT_DO_BED_DEF;
  doBedIso = DEFAULT_DO_BED_ISO;
  bedDefIntervalYears = DEFAULT_BED_DEF_INTERVAL_YEARS;
  noSpokesLevel = DEFAULT_NOSPOKESLEVEL;

  // set default locations of soundings and slices
  id = (grid.Mx - 1)/2;
  jd = (grid.My - 1)/2;
  kd = 0;

  // default polar stereographic projection settings: South Pole
  psParams.svlfp = 0.0;
  psParams.lopo = 90.0;
  psParams.sp = -71.0;

  return 0;
}
