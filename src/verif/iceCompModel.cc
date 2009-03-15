// Copyright (C) 2004-2009 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <cmath>
#include <cstring>
#include <petscda.h>

#include "exactTestsABCDE.h"
#include "exactTestsFG.h" 
#include "exactTestH.h" 
#include "exactTestL.h" 
#include "../num/extrasGSL.hh"

#include "../coupler/pccoupler.hh"
#include "iceCompModel.hh"

const PetscScalar IceCompModel::ablationRateOutside = 0.02; // m/a

IceCompModel::IceCompModel(IceGrid &g, int mytest)
  : IceModel(g), tgaIce(NULL) {
  
  // note lots of defaults are set by the IceModel constructor
  // defaults for IceCompModel:  
  testname = mytest;
  exactOnly = PETSC_FALSE;
  bedrock_is_ice_forK = PETSC_FALSE;
  
  // Override some defaults from parent class
  enhancementFactor = 1.0;

  // set values of flags in run() 
  doMassConserve = PETSC_TRUE;
  useSSAVelocity = PETSC_FALSE;
  includeBMRinContinuity = PETSC_FALSE;
  doPlasticTill = PETSC_FALSE;
}


IceCompModel::~IceCompModel() {
}


PetscErrorCode IceCompModel::set_grid_defaults() {
  PetscErrorCode ierr;

  // This sets the defaults for each test; command-line options can override this.

  switch (testname) {
  case 'A':
  case 'E':
    // use 1600km by 1600km by 4000m rectangular domain
    grid.Lx = grid.Ly = 800e3;
    grid.Lz = 4000;
    break;
  case 'B':
  case 'H':
    // use 2400km by 2400km by 4000m rectangular domain
    grid.Lx = grid.Ly = 1200e3;
    grid.Lz = 4000;
    break;
  case 'C':
  case 'D':
    // use 2000km by 2000km by 4000m rectangular domain
    grid.Lx = grid.Ly = 1000e3;
    grid.Lz = 4000;
    break;
  case 'F':
  case 'G':
  case 'L':
    // use 1800km by 1800km by 4000m rectangular domain
    grid.Lx = grid.Ly = 900e3;
    grid.Lz = 4000;
    break;
  case 'K':
    // use 2000km by 2000km by 4000m rectangular domain, but make truely periodic
    grid.Mbz = 2;
    grid.Lx = grid.Ly = 1000e3;
    grid.Lz = 4000;
    grid.periodicity = XY_PERIODIC;
    break;
  default:
    ierr = PetscPrintf(grid.com, "IceCompModel ERROR : desired test not implemented\n");
    CHKERRQ(ierr);
    PetscEnd();
  }

  return 0;
}


PetscErrorCode IceCompModel::set_grid_from_options() {
  PetscErrorCode ierr;

  // Allows user to set -Mx, -My, -Mz, -Mbz, -Lx, -Ly, -Lz, -chebZ and -quadZ.
  ierr = IceModel::set_grid_from_options(); CHKERRQ(ierr);

  if (testname == 'K') {
    if (grid.Mbz == 1) {
      ierr = PetscPrintf(grid.com, "PISM ERROR: grid.Mbz must be at least two in Test K\n");
      CHKERRQ(ierr);
      PetscEnd();
    }

    // now, if unequal spaced vertical then run special code to set bedrock vertical 
    //   levels so geothermal boundary condition is imposed at exact depth 1000m
    if (grid.vertical_spacing != EQUAL) {
      ierr = verbPrintf(2,grid.com,"setting vertical levels so bottom at -1000 m ...\n"); 
      CHKERRQ(ierr);
      grid.Lbz = 1000.0;
      const PetscScalar dzbEQ = grid.Lbz / ((PetscScalar) grid.Mbz - 1);
      for (PetscInt kb=0; kb < grid.Mbz; kb++) {
	grid.zblevels[kb] = -grid.Lbz + dzbEQ * ((PetscScalar) kb);
      }
      grid.zblevels[grid.Mbz - 1] = 0.0;
    }
  }

  return 0;
}


PetscErrorCode IceCompModel::setFromOptions() {
  PetscErrorCode ierr;

  ierr = verbPrintf(2, grid.com, "starting Test %c ...\n", testname);  CHKERRQ(ierr);

  /* This switch turns off actual numerical evolution and simply reports the
     exact solution. */
  ierr = PetscOptionsHasName(PETSC_NULL, "-eo", &exactOnly); CHKERRQ(ierr);
  if (exactOnly == PETSC_TRUE) {
    ierr = verbPrintf(1,grid.com, "!!EXACT SOLUTION ONLY, NO NUMERICAL SOLUTION!!\n");
             CHKERRQ(ierr);
  }

  // These ifs are here (and not in the constructor or init_physics()) because
  // testname actually comes from a command-line *and* because command-line
  // options should be able to override parameter values set here.

  if (testname == 'H') {
    doBedDef = PETSC_TRUE;
    doBedIso = PETSC_TRUE;
  } else
    doBedDef = PETSC_FALSE;  

  if ((testname == 'F') || (testname == 'G') || (testname == 'K')) {
    doTemp = PETSC_TRUE;
    globalMinAllowedTemp = 0.0;  // essentially turn off run-time reporting of extremely
    maxLowTempCount = 1000000;   // low computed temperatures; *they will be reported
                                 // as errors* anyway
  } else
    doTemp = PETSC_FALSE; 

  if ((testname == 'A') || (testname == 'E')) {
    isDrySimulation = PETSC_TRUE;
    doOceanKill = PETSC_TRUE;
  } else {
    isDrySimulation = PETSC_TRUE;
    doOceanKill = PETSC_FALSE;
  }

  // special considerations for K wrt thermal bedrock and pressure-melting
  if (testname == 'K') {
    thermalBedrock = PETSC_TRUE;
    allowAboveMelting = PETSC_FALSE;
    reportHomolTemps = PETSC_TRUE;
  } else {
    thermalBedrock = PETSC_FALSE;
    // note temps are generally allowed to go above pressure melting in verify
    allowAboveMelting = PETSC_TRUE;
    reportHomolTemps = PETSC_FALSE;
  }

  ierr = IceModel::setFromOptions();CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceCompModel::init_physics() {
  PetscErrorCode ierr;

  // Set the default for IceCompModel:
  ierr = iceFactory.setType(ICE_ARR); CHKERRQ(ierr);

  // Let the base class version read the options (possibly overriding the
  // default set above) and create the IceType object.
  ierr = IceModel::init_physics(); CHKERRQ(ierr);
  
  // check on whether the options (already checked) chose the right IceType for verification;
  //   need to have a tempFromSoftness() procedure as well as the need for the right
  //   flow law to have the errors make sense
  tgaIce = dynamic_cast<ThermoGlenArrIce*>(ice);
  if (!tgaIce) SETERRQ(1,"IceCompModel requires ThermoGlenArrIce or a derived class");
  if (IceTypeIsPatersonBuddCold(ice) == PETSC_FALSE) {
    ierr = verbPrintf(1, grid.com, 
       "WARNING: user set -law or -gk; default flow law should be -ice_type arr for IceCompModel\n");
    CHKERRQ(ierr);
  }

  f = ice->rho / bed_deformable.rho;  // for simple isostasy

  if (testname != 'K') {
    // now make bedrock have same material properties as ice
    // (note Mbz=1 also, by default, but want ice/rock interface to see
    // pure ice from the point of view of applying geothermal boundary
    // condition, especially in tests F and G)
    bed_thermal.rho = tgaIce->rho;
    bed_thermal.c_p = tgaIce->c_p;
    bed_thermal.k = tgaIce->k;
  }

  if ( (testname == 'H') && ((doBedDef == PETSC_FALSE) || (doBedIso == PETSC_FALSE)) ) {
    ierr = verbPrintf(1,grid.com, 
           "IceCompModel WARNING: Test H should be run with option\n"
           "  -bed_def_iso  for the reported errors to be correct.\n"); CHKERRQ(ierr);
  }

  // switch changes Test K to make material properties for bedrock the same as for ice
  PetscTruth biiSet;
  ierr = PetscOptionsHasName(PETSC_NULL, "-bedrock_is_ice", &biiSet); CHKERRQ(ierr);
  if (biiSet == PETSC_TRUE) {
    if (testname == 'K') {
      ierr = verbPrintf(1,grid.com,
         "setting material properties of bedrock to those of ice in Test K\n"); CHKERRQ(ierr);
      bed_thermal.rho = tgaIce->rho;
      bed_thermal.c_p = tgaIce->c_p;
      bed_thermal.k = tgaIce->k;
      bedrock_is_ice_forK = PETSC_TRUE;
    } else {
      ierr = verbPrintf(1,grid.com,
         "IceCompModel WARNING: option -bedrock_is_ice ignored; only applies to Test K\n");
         CHKERRQ(ierr);
    }
  }

  return 0;
}


PetscErrorCode IceCompModel::init_couplers() {
  PetscErrorCode ierr;

  PetscTruth i_set;
  char filename[PETSC_MAX_PATH_LEN];
  ierr = PetscOptionsGetString(PETSC_NULL, "-i",
			       filename, PETSC_MAX_PATH_LEN, &i_set); CHKERRQ(ierr);
  if (i_set) {
    ierr = verbPrintf(2, grid.com, "starting Test %c climate using -i file %s ...\n",
	      testname, filename);  CHKERRQ(ierr);
    PISMConstAtmosCoupler *pcac = dynamic_cast<PISMConstAtmosCoupler*>(atmosPCC);   
    pcac->initializeFromFile = true;
  }

  ierr = IceModel::init_couplers(); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceCompModel::set_vars_from_options() {
  PetscErrorCode ierr;

  // -boot_from command-line option is not allowed here.
  ierr = stop_if_set("-boot_from"); CHKERRQ(ierr);

  ierr = SigmaComp3.set(0.0); CHKERRQ(ierr);

  ierr = verbPrintf(3,grid.com, "initializing Test %c from formulas ...\n",testname);  CHKERRQ(ierr);

  // none use Goldsby-Kohlstedt or do age calc
  setInitialAgeYears(initial_age_years_default);

  // all have no uplift or Hmelt
  ierr = vuplift.set(0.0); CHKERRQ(ierr);
  ierr = vHmelt.set(0.0); CHKERRQ(ierr);
  ierr = vbasalMeltRate.set(0.0); CHKERRQ(ierr);

  // Test-specific initialization:
  switch (testname) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'E':
  case 'H':
    ierr = initTestABCDEH(); CHKERRQ(ierr);
    break;
  case 'F':
  case 'G':
    ierr = initTestFG(); CHKERRQ(ierr);  // see iCMthermo.cc
    break;
  case 'K':
    ierr = initTestK(); CHKERRQ(ierr);  // see iCMthermo.cc
    break;
  case 'L':
    ierr = initTestL(); CHKERRQ(ierr);
    break;
  default:  SETERRQ(1,"Desired test not implemented by IceCompModel.\n");
  }

  return 0;
}


void IceCompModel::mapcoords(const PetscInt i, const PetscInt j,
                             PetscScalar &x, PetscScalar &y, PetscScalar &r) {
  // compute x,y,r on grid from i,j
  PetscScalar ifrom0, jfrom0;

  ifrom0=static_cast<PetscScalar>(i)-static_cast<PetscScalar>(grid.Mx - 1)/2.0;
  jfrom0=static_cast<PetscScalar>(j)-static_cast<PetscScalar>(grid.My - 1)/2.0;
  x=grid.dx*ifrom0;
  y=grid.dy*jfrom0;
  r = sqrt(PetscSqr(x) + PetscSqr(y));
}


// reimplement IceModel::basalVelocity(), for E
PetscScalar IceCompModel::basalVelocity(const PetscScalar xIN, const PetscScalar yIN,
                                        const PetscScalar H, const PetscScalar T,
                                        const PetscScalar alpha, const PetscScalar muIN) const {
  // note: ignores T and muIN

  if (testname == 'E') {
    //PetscErrorCode  ierr = PetscPrintf(grid.com, 
    //        "   [IceCompModel::basal called with:   x=%f, y=%f, H=%f, T=%f, alpha=%f]\n",
    //        xIN,yIN,H,alpha);  CHKERRQ(ierr);
    const PetscScalar pi = 3.14159265358979;
    const PetscScalar r1 = 200e3, r2 = 700e3,   /* define region of sliding */
                      theta1 = 10 * (pi/180), theta2 = 40 * (pi/180);
    const PetscScalar x = fabs(xIN), y = fabs(yIN);
    const PetscScalar r = sqrt(x * x + y * y);
    PetscScalar       theta;
    if (x < 1.0)
      theta = pi / 2.0;
    else
      theta = atan(y / x);
  
    if ((r > r1) && (r < r2) && (theta > theta1) && (theta < theta2)) {
      // now INSIDE sliding region
      const PetscScalar rbot = (r2 - r1) * (r2 - r1),
                        thetabot = (theta2 - theta1) * (theta2 - theta1);
      const PetscScalar mu_max = 2.5e-11; /* Pa^-1 m s^-1; max sliding coeff */
      PetscScalar muE = mu_max * (4.0 * (r - r1) * (r2 - r) / rbot) 
                               * (4.0 * (theta - theta1) * (theta2 - theta) / thetabot);
      return muE * tgaIce->rho * earth_grav * H;
    } else
      return 0.0;
  } else
    return 0.0;  // zero sliding for other tests
}


PetscErrorCode IceCompModel::initTestABCDEH() {
  PetscErrorCode  ierr;
  PetscScalar     A0, T0, **H, **accum, **mask, dummy1, dummy2, dummy3;
  const PetscScalar LforAE = 750e3; // m

  // need pointers to surface temp and accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccTs, *pccaccum;
  ierr = atmosPCC->updateSurfTempAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccTs); CHKERRQ(ierr);  
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  // compute T so that A0 = A(T) = Acold exp(-Qcold/(R T))  (i.e. for ThermoGlenArrIce);
  // set all temps to this constant
  A0 = 1.0e-16/secpera;    // = 3.17e-24  1/(Pa^3 s);  (EISMINT value) flow law parameter
  T0 = tgaIce->tempFromSoftness(A0);
  ierr = pccTs->set(T0); CHKERRQ(ierr);
  ierr =   T3.set(T0); CHKERRQ(ierr);
  ierr =  Tb3.set(T0); CHKERRQ(ierr);
  ierr = vGhf.set(Ggeo); CHKERRQ(ierr);
  
  ierr = vMask.set(MASK_SHEET); CHKERRQ(ierr);
  if (testname == 'E') {  // value is not used by IceCompModel::basalVelocity() below,
    muSliding = 1.0;      //    but this acts as flag to allow sliding
  } else {
    muSliding = 0.0;
  }

  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  ierr = vH.get_array(H); CHKERRQ(ierr);
  if ((testname == 'A') || (testname == 'E')) {
    ierr = vMask.get_array(mask); CHKERRQ(ierr);
  }
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      switch (testname) {
        case 'A':
          exactA(r,&H[i][j],&accum[i][j]);
          if (r >= LforAE)
            mask[i][j] = MASK_FLOATING_OCEAN0;
          break;
        case 'B':
          exactB(grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        case 'C':
          exactC(grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        case 'D':
          exactD(grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        case 'E':
          exactE(xx,yy,&H[i][j],&accum[i][j],&dummy1,&dummy2,&dummy3);
          if (r >= LforAE)
            mask[i][j] = MASK_FLOATING_OCEAN0;
          break;
        case 'H':
          exactH(f,grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        default:  SETERRQ(1,"test must be A, B, C, D, E, or H");
      }
    }
  }
  ierr = pccaccum->end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);
  if ((testname == 'A') || (testname == 'E')) {
    ierr = vMask.end_access(); CHKERRQ(ierr);
  }

  ierr = vH.beginGhostComm(); CHKERRQ(ierr);
  ierr = vH.endGhostComm(); CHKERRQ(ierr);

  if (testname == 'H') {
    ierr = vH.copy_to(vh); CHKERRQ(ierr);
    ierr = vh.scale(1-f); CHKERRQ(ierr);
    ierr = vH.copy_to(vbed); CHKERRQ(ierr);
    ierr = vbed.scale(-f); CHKERRQ(ierr);
  } else {  // flat bed case otherwise
    ierr = vH.copy_to(vh); CHKERRQ(ierr);
    ierr = vbed.set(0.0); CHKERRQ(ierr);
  }

  return 0;
}


PetscErrorCode IceCompModel::initTestL() {
  PetscErrorCode  ierr;
  PetscScalar     A0, T0, **H, **accum, **bed;

  if (testname != 'L')  { SETERRQ(1,"test must be 'L'"); }
  
  // need pointers to surface temp and accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccTs, *pccaccum;
  ierr = atmosPCC->updateSurfTempAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccTs); CHKERRQ(ierr);  
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  // compute T so that A0 = A(T) = Acold exp(-Qcold/(R T))  (i.e. for ThermoGlenArrIce);
  // set all temps to this constant
  A0 = 1.0e-16/secpera;    // = 3.17e-24  1/(Pa^3 s);  (EISMINT value) flow law parameter
  T0 = tgaIce->tempFromSoftness(A0);
  ierr = pccTs->set(T0); CHKERRQ(ierr);
  ierr =   T3.set(T0); CHKERRQ(ierr);
  ierr =  Tb3.set(T0); CHKERRQ(ierr);
  ierr = vGhf.set(Ggeo); CHKERRQ(ierr);
  
  ierr = vMask.set(MASK_SHEET); CHKERRQ(ierr);
  muSliding = 0.0;  // note reimplementation of basalVelocity()

  // setup to evaluate test L; requires solving an ODE numerically
  const PetscInt  MM = grid.xm * grid.ym;
  double          *rr, *HH, *bb, *aa;
  int             *ia, *ja;
  rr = new double[MM];  
  HH = new double[MM];  bb = new double[MM];  aa = new double[MM];
  ia = new int[MM];  ja = new int[MM];

  for (PetscInt i = 0; i < grid.xm; i++) {
    for (PetscInt j = 0; j < grid.ym; j++) {
      const PetscInt  k = i * grid.ym + j;
      PetscScalar  junkx, junky;
      mapcoords(i + grid.xs, j + grid.ys, junkx, junky, rr[k]);
      rr[k] = - rr[k];
      ia[k] = i + grid.xs;  ja[k] = j + grid.ys;
    }
  }

  heapsort_double_2indfollow(rr,ia,ja,MM);  // sorts into ascending;  O(MM log MM) time
  for (PetscInt k = 0; k < MM; k++)   rr[k] = -rr[k];   // now descending

  // get soln to test L at these points; solves ODE only once (on each processor)
  ierr = exactL_list(rr, MM, HH, bb, aa);  CHKERRQ(ierr);
  
  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = vbed.get_array(bed); CHKERRQ(ierr);
  for (PetscInt k = 0; k < MM; k++) {
    const PetscInt i = ia[k],  j = ja[k];
    H[i][j] = HH[k];
    bed[i][j] = bb[k];
    accum[i][j] = aa[k];
  }
  ierr = pccaccum->end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vbed.end_access(); CHKERRQ(ierr);

  delete [] rr;  delete [] HH;  delete [] bb;  delete [] aa;  delete [] ia;  delete [] ja; 

  ierr = vH.beginGhostComm(); CHKERRQ(ierr);
  ierr = vH.endGhostComm(); CHKERRQ(ierr);
  ierr = vbed.beginGhostComm(); CHKERRQ(ierr);
  ierr = vbed.endGhostComm(); CHKERRQ(ierr);

  // set surface to H+b
  ierr = vH.add(1.0, vbed, vh); CHKERRQ(ierr);
  ierr = vh.beginGhostComm(); CHKERRQ(ierr);
  ierr = vh.endGhostComm(); CHKERRQ(ierr);

  // store copy of vH for "-eo" runs and for evaluating geometry errors
  ierr = vH.copy_to(vHexactL); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceCompModel::getCompSourcesTestCDH() {
  PetscErrorCode  ierr;
  PetscScalar     **accum, dummy;

  // need pointer to accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccaccum;
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  // before flow step, set accumulation from exact values;
  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      switch (testname) {
        case 'C':
          exactC(grid.year*secpera,r,&dummy,&accum[i][j]);
          break;
        case 'D':
          exactD(grid.year*secpera,r,&dummy,&accum[i][j]);
          break;
        case 'H':
          exactH(f,grid.year*secpera,r,&dummy,&accum[i][j]);
          break;
        default:  SETERRQ(1,"testname must be C, D, or H");
      }
    }
  }
  ierr = pccaccum->end_access(); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceCompModel::fillSolnTestABCDH() {
  PetscErrorCode  ierr;
  PetscScalar     **H, **accum;

  // need pointer to accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccaccum;
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  ierr = vH.get_array(H); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      switch (testname) {
        case 'A':
          exactA(r,&H[i][j],&accum[i][j]);
          break;
        case 'B':
          exactB(grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        case 'C':
          exactC(grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        case 'D':
          exactD(grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        case 'H':
          exactH(f,grid.year*secpera,r,&H[i][j],&accum[i][j]);
          break;
        default:  SETERRQ(1,"test must be A, B, C, D, or H");
      }
    }
  }

  ierr = pccaccum->end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);

  ierr = vH.beginGhostComm(); CHKERRQ(ierr);
  ierr = vH.endGhostComm(); CHKERRQ(ierr);

  if (testname == 'H') {
    ierr = vH.copy_to(vh); CHKERRQ(ierr);
    ierr = vh.scale(1-f); CHKERRQ(ierr);
    ierr = vH.copy_to(vbed); CHKERRQ(ierr);
    ierr = vbed.scale(-f); CHKERRQ(ierr);
    ierr = vbed.beginGhostComm(); CHKERRQ(ierr);
    ierr = vbed.endGhostComm(); CHKERRQ(ierr);
  } else {
    ierr = vH.copy_to(vh); CHKERRQ(ierr);
  }
  ierr = vh.beginGhostComm(); CHKERRQ(ierr);
  ierr = vh.endGhostComm(); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceCompModel::fillSolnTestE() {
  PetscErrorCode  ierr;
  PetscScalar     **H, **accum, **ub, **vb, dummy;

  // need pointer to accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccaccum;
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = vub.get_array(ub); CHKERRQ(ierr);
  ierr = vvb.get_array(vb); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      exactE(xx,yy,&H[i][j],&accum[i][j],&dummy,&ub[i][j],&vb[i][j]);
    }
  }
  ierr = pccaccum->end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vub.end_access(); CHKERRQ(ierr);
  ierr = vvb.end_access(); CHKERRQ(ierr);

  ierr = vH.beginGhostComm(); CHKERRQ(ierr);
  ierr = vH.endGhostComm(); CHKERRQ(ierr);
  ierr = vH.copy_to(vh); CHKERRQ(ierr);

  ierr = vub.beginGhostComm(); CHKERRQ(ierr);
  ierr = vub.endGhostComm(); CHKERRQ(ierr);
  ierr = vvb.beginGhostComm(); CHKERRQ(ierr);
  ierr = vvb.endGhostComm(); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IceCompModel::fillSolnTestL() {
  PetscErrorCode  ierr;

  ierr = vHexactL.beginGhostComm(); CHKERRQ(ierr);
  ierr = vHexactL.endGhostComm(); CHKERRQ(ierr);
  ierr = vH.copy_from(vHexactL);

  ierr = vbed.add(1.0, vH, vh);	CHKERRQ(ierr); //  h = H + bed = 1 * H + bed
  ierr = vh.beginGhostComm(); CHKERRQ(ierr);
  ierr = vh.endGhostComm(); CHKERRQ(ierr);

  // note bed was filled at initialization and hasn't changed
  return 0;
}


PetscErrorCode IceCompModel::computeGeometryErrors(
      PetscScalar &gvolexact, PetscScalar &gareaexact, PetscScalar &gdomeHexact,
      PetscScalar &volerr, PetscScalar &areaerr,
      PetscScalar &gmaxHerr, PetscScalar &gavHerr, PetscScalar &gmaxetaerr,
      PetscScalar &centerHerr) {
  // compute errors in thickness, eta=thickness^{(2n+2)/n}, volume, area
  
  PetscErrorCode  ierr;
  PetscScalar     **H, **HexactL;
  PetscScalar     Hexact, vol, area, domeH, volexact, areaexact, domeHexact;
  PetscScalar     Herr, avHerr, etaerr;

  PetscScalar     dummy, z, dummy1, dummy2, dummy3, dummy4, dummy5;

  ierr = vH.get_array(H); CHKERRQ(ierr);
  if (testname == 'L') {
    ierr = vHexactL.get_array(HexactL); CHKERRQ(ierr);
  }

  vol = 0; area = 0; domeH = 0;
  volexact = 0; areaexact = 0; domeHexact = 0;
  Herr = 0; avHerr=0; etaerr = 0;

  // area of grid square in square km:
  const PetscScalar   a = grid.dx * grid.dy * 1e-3 * 1e-3;
  const PetscScalar   m = (2.0 * tgaIce->exponent() + 2.0) / tgaIce->exponent();
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (H[i][j] > 0) {
        area += a;
        vol += a * H[i][j] * 1e-3;
      }
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      switch (testname) {
        case 'A':
          exactA(r,&Hexact,&dummy);
          break;
        case 'B':
          exactB(grid.year*secpera,r,&Hexact,&dummy);
          break;
        case 'C':
          exactC(grid.year*secpera,r,&Hexact,&dummy);
          break;
        case 'D':
          exactD(grid.year*secpera,r,&Hexact,&dummy);
          break;
        case 'E':
          exactE(xx,yy,&Hexact,&dummy,&dummy1,&dummy2,&dummy3);
          break;
        case 'F':
          if (r > LforFG - 1.0) {  // outside of sheet
            Hexact=0.0;
          } else {
            r=PetscMax(r,1.0);
            z=0.0;
            bothexact(0.0,r,&z,1,0.0,
                      &Hexact,&dummy,&dummy5,&dummy1,&dummy2,&dummy3,&dummy4);
          }
          break;
        case 'G':
          if (r > LforFG -1.0) {  // outside of sheet
            Hexact=0.0;
          } else {
            r=PetscMax(r,1.0);
            z=0.0;
            bothexact(grid.year*secpera,r,&z,1,ApforG,
                      &Hexact,&dummy,&dummy5,&dummy1,&dummy2,&dummy3,&dummy4);
          }
          break;
        case 'H':
          exactH(f,grid.year*secpera,r,&Hexact,&dummy);
          break;
        case 'K':
          Hexact = 3000.0;
          break;
        case 'L':
          Hexact = HexactL[i][j];
          break;
        default:  SETERRQ(1,"test must be A, B, C, D, E, F, G, H, K, or L");
      }

      if (Hexact > 0) {
        areaexact += a;
        volexact += a * Hexact * 1e-3;
      }
      if (i == (grid.Mx - 1)/2 && j == (grid.My - 1)/2) {
        domeH = H[i][j];
        domeHexact = Hexact;
      }
      // compute maximum errors
      Herr = PetscMax(Herr,PetscAbsReal(H[i][j] - Hexact));
      etaerr = PetscMax(etaerr,PetscAbsReal(pow(H[i][j],m) - pow(Hexact,m)));
      // add to sums for average errors
      avHerr += PetscAbsReal(H[i][j] - Hexact);
    }
  }

  ierr = vH.end_access(); CHKERRQ(ierr);
  if (testname == 'L') {
    ierr = vHexactL.end_access(); CHKERRQ(ierr);
  }
  
  // globalize (find errors over all processors) 
  PetscScalar gvol, garea, gdomeH;
  ierr = PetscGlobalSum(&volexact, &gvolexact, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&domeHexact, &gdomeHexact, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&areaexact, &gareaexact, grid.com); CHKERRQ(ierr);

  ierr = PetscGlobalSum(&vol, &gvol, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&area, &garea, grid.com); CHKERRQ(ierr);
  volerr = PetscAbsReal(gvol - gvolexact);
  areaerr = PetscAbsReal(garea - gareaexact);

  ierr = PetscGlobalMax(&Herr, &gmaxHerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avHerr, &gavHerr, grid.com); CHKERRQ(ierr);
  gavHerr = gavHerr/(grid.Mx*grid.My);
  ierr = PetscGlobalMax(&etaerr, &gmaxetaerr, grid.com); CHKERRQ(ierr);
  
  ierr = PetscGlobalMax(&domeH, &gdomeH, grid.com); CHKERRQ(ierr);
  centerHerr = PetscAbsReal(gdomeH - gdomeHexact);
  
  return 0;
}


PetscErrorCode IceCompModel::computeBasalVelocityErrors(
      PetscScalar &exactmaxspeed,
      PetscScalar &gmaxvecerr, PetscScalar &gavvecerr,
      PetscScalar &gmaxuberr, PetscScalar &gmaxvberr) {

  PetscErrorCode ierr;
  PetscScalar    **H, **ub, **vb;
  PetscScalar    maxvecerr, avvecerr, maxuberr, maxvberr;
  PetscScalar    ubexact,vbexact, dummy1,dummy2,dummy3;
  
  if (testname != 'E')
    SETERRQ(1,"basal velocity errors only computable for test E\n");

  ierr = vub.get_array(ub); CHKERRQ(ierr);
  ierr = vvb.get_array(vb); CHKERRQ(ierr);
  ierr = vH.get_array(H); CHKERRQ(ierr);
  maxvecerr = 0.0; avvecerr = 0.0; maxuberr = 0.0; maxvberr = 0.0;
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; i++) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; j++) {
      if (H[i][j] > 0.0) {
        PetscScalar r,xx,yy;
        mapcoords(i,j,xx,yy,r);
        exactE(xx,yy,&dummy1,&dummy2,&dummy3,&ubexact,&vbexact); 
        // compute maximum errors
        const PetscScalar uberr = PetscAbsReal(ub[i][j] - ubexact);
        const PetscScalar vberr = PetscAbsReal(vb[i][j] - vbexact);
        maxuberr = PetscMax(maxuberr,uberr);
        maxvberr = PetscMax(maxvberr,vberr);
        const PetscScalar vecerr = sqrt(uberr*uberr + vberr*vberr);
        maxvecerr = PetscMax(maxvecerr,vecerr);
        avvecerr += vecerr;      
      }
    }
  }
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vub.end_access(); CHKERRQ(ierr);
  ierr = vvb.end_access(); CHKERRQ(ierr);

  ierr = PetscGlobalMax(&maxuberr, &gmaxuberr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&maxvberr, &gmaxvberr, grid.com); CHKERRQ(ierr);

  ierr = PetscGlobalMax(&maxvecerr, &gmaxvecerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avvecerr, &gavvecerr, grid.com); CHKERRQ(ierr);
  gavvecerr = gavvecerr/(grid.Mx*grid.My);
  
  const PetscScalar pi = 3.14159265358979;
  const PetscScalar xpeak = 450e3 * cos(25.0*(pi/180.0)),
                    ypeak = 450e3 * sin(25.0*(pi/180.0));
  exactE(xpeak,ypeak,&dummy1,&dummy2,&dummy3,&ubexact,&vbexact);
  exactmaxspeed = sqrt(ubexact*ubexact + vbexact*vbexact);
  return 0;
}


PetscErrorCode IceCompModel::additionalAtStartTimestep() {
  PetscErrorCode    ierr;
  
  ierr = verbPrintf(5,grid.com,
               "additionalAtStartTimestep() in IceCompModel entered with test %c",
               testname); CHKERRQ(ierr);

  if (exactOnly == PETSC_TRUE)
    dt_force = maxdt;

  // these have no changing boundary conditions or comp sources:
  if (strchr("ABEKL",testname) != NULL) 
    return 0;

  switch (testname) {
    case 'C':
    case 'D':
    case 'H':
      ierr = getCompSourcesTestCDH();
      break;
    case 'F':
    case 'G':
      ierr = getCompSourcesTestFG();  // see iCMthermo.cc
      break;
    default:  SETERRQ(1,"only tests CDHFG have comp source update at start time step\n");
  }

  return 0;
}


PetscErrorCode IceCompModel::additionalAtEndTimestep() {
  PetscErrorCode    ierr;
  
  ierr = verbPrintf(5,grid.com,
               "additionalAtEndTimestep() in IceCompModel entered with test %c",testname);
               CHKERRQ(ierr);

  // do nothing at the end of the time step unless the user has asked for the 
  // exact solution to overwrite the numerical solution
  if (exactOnly == PETSC_FALSE)  
    return 0;

  // because user wants exact solution, fill gridded values from exact formulas;
  // important notes: 
  //     (1) the numerical computation *has* already occurred, in run(),
  //           and we just overwrite it with the exact solution here
  //     (2) certain diagnostic quantities like dHdt are computed numerically,
  //           and not overwritten here; while cbar,csurf,cflx,wsurf are diagnostic
  //           quantities recomputed at the end of the run for writing into
  //           NetCDF, in particular dHdt is not recomputed before being written
  //           into the output file, so it is actually numerical
  switch (testname) {
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'H':
      ierr = fillSolnTestABCDH();
      break;
    case 'E':
      ierr = fillSolnTestE();
      break;
    case 'F':
    case 'G':
      ierr = fillSolnTestFG();  // see iCMthermo.cc
      break;
    case 'K':
      ierr = fillSolnTestK();  // see iCMthermo.cc
      break;
    case 'L':
      ierr = fillSolnTestL();
      break;
    default:  SETERRQ(1,"unknown testname in IceCompModel");
  }

  return 0;
}


PetscErrorCode IceCompModel::summaryPrintLine(
    const PetscTruth printPrototype, const PetscTruth tempAndAge,
    const PetscScalar year, const PetscScalar dt, 
    const PetscScalar volume_kmcube, const PetscScalar area_kmsquare,
    const PetscScalar meltfrac, const PetscScalar H0, const PetscScalar T0) {

  PetscErrorCode ierr;
  if (printPrototype == PETSC_TRUE) {
    if ((testname == 'F') || (testname == 'G') || (testname == 'K')) {
      ierr = verbPrintf(2,grid.com,
               "P         YEAR:      ivol    iarea meltfABS    thick0     temp0\n");
      ierr = verbPrintf(2,grid.com,
               "U        years  10^6_km^3 10^6_km^2  (none)         m         K\n");
    } else {
      ierr = verbPrintf(2,grid.com,
               "P         YEAR:      ivol    iarea    thick0\n");
      ierr = verbPrintf(2,grid.com,
               "U        years  10^6_km^3 10^6_km^2        m\n");
    }
  } else {
    if ((testname == 'F') || (testname == 'G') || (testname == 'K')) {
      if (tempAndAge == PETSC_TRUE) {
        ierr = verbPrintf(2,grid.com, "S %12.5f: %9.5f %8.4f %8.4f %9.3f %9.4f\n",
                       year, volume_kmcube/1.0e6,area_kmsquare/1.0e6,meltfrac,H0,T0); CHKERRQ(ierr);
      } else {
        ierr = verbPrintf(2,grid.com, "S %12.5f: %9.5f %8.4f   <same> %9.3f    <same>\n",
                       year, volume_kmcube/1.0e6,area_kmsquare/1.0e6,H0); CHKERRQ(ierr);
      }
    } else {
        ierr = verbPrintf(2,grid.com, "S %12.5f: %9.5f %8.4f %9.3f\n",
           year, volume_kmcube/1.0e6, area_kmsquare/1.0e6, H0); CHKERRQ(ierr);
    }
  }
  return 0;
}


PetscErrorCode IceCompModel::reportErrors() {
  // geometry errors to report (for all tests): 
  //    -- max thickness error
  //    -- average (at each grid point on whole grid) thickness error
  //    -- max (thickness)^(2n+2)/n error
  //    -- volume error
  //    -- area error
  // and temperature errors (for tests F & G):
  //    -- max T error over 3D domain of ice
  //    -- av T error over 3D domain of ice
  // and basal temperature errors (for tests F & G):
  //    -- max basal temp error
  //    -- average (at each grid point on whole grid) basal temp error
  // and strain-heating (Sigma) errors (for tests F & G):
  //    -- max Sigma error over 3D domain of ice (in 10^-3 K a^-1)
  //    -- av Sigma error over 3D domain of ice (in 10^-3 K a^-1)
  // and surface velocity errors (for tests F & G):
  //    -- max |<us,vs> - <usex,vsex>| error
  //    -- av |<us,vs> - <usex,vsex>| error
  //    -- max ws error
  //    -- av ws error
  // and basal sliding errors (for test E):
  //    -- max ub error
  //    -- max vb error
  //    -- max |<ub,vb> - <ubexact,vbexact>| error
  //    -- av |<ub,vb> - <ubexact,vbexact>| error

  PetscErrorCode  ierr;
  ierr = verbPrintf(1,grid.com, 
     "NUMERICAL ERRORS evaluated at final time (relative to exact solution):\n");
     CHKERRQ(ierr);

  // geometry (thickness, vol) errors if appropriate; reported in m except for relmaxETA
  if (testname != 'K') {
    PetscScalar volexact, areaexact, domeHexact, volerr, areaerr, maxHerr, avHerr,
                maxetaerr, centerHerr;
    ierr = computeGeometryErrors(volexact,areaexact,domeHexact,
                                 volerr,areaerr,maxHerr,avHerr,maxetaerr,centerHerr);
            CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, 
            "geometry  :    prcntVOL        maxH         avH   relmaxETA\n");
            CHKERRQ(ierr);  // no longer reporting centerHerr
    const PetscScalar   m = (2.0 * tgaIce->exponent() + 2.0) / tgaIce->exponent();
    ierr = verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n",
                      100*volerr/volexact, maxHerr, avHerr,
                      maxetaerr/pow(domeHexact,m)); CHKERRQ(ierr);
  }

  // temperature errors if appropriate; reported in K
  if ((testname == 'F') || (testname == 'G')) {
    PetscScalar maxTerr, avTerr, basemaxTerr, baseavTerr, basecenterTerr;
    ierr = computeTemperatureErrors(maxTerr, avTerr); CHKERRQ(ierr);
    ierr = computeBasalTemperatureErrors(basemaxTerr, baseavTerr, basecenterTerr);
       CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, 
       "temp      :        maxT         avT    basemaxT     baseavT\n");
       CHKERRQ(ierr);  // no longer reporting   basecenterT
    ierr = verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n", 
       maxTerr, avTerr, basemaxTerr, baseavTerr); CHKERRQ(ierr);
  } else if (testname == 'K') {
    PetscScalar maxTerr, avTerr, maxTberr, avTberr;
    ierr = computeIceBedrockTemperatureErrors(maxTerr, avTerr, maxTberr, avTberr);
       CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, 
       "temp      :        maxT         avT       maxTb        avTb\n"); CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n", 
                  maxTerr, avTerr, maxTberr, avTberr); CHKERRQ(ierr);
  }

  // Sigma errors if appropriate; reported in 10^6 J/(s m^3)
  if ((testname == 'F') || (testname == 'G')) {
    PetscScalar maxSigerr, avSigerr;
    ierr = computeSigmaErrors(maxSigerr, avSigerr); CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, 
       "Sigma     :      maxSig       avSig\n"); CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, "           %12.6f%12.6f\n", 
                  maxSigerr*1.0e6, avSigerr*1.0e6); CHKERRQ(ierr);
  }

  // surface velocity errors if exact values are available; reported in m/a
  if ((testname == 'F') || (testname == 'G')) {
    PetscScalar maxUerr, avUerr, maxWerr, avWerr;
    ierr = computeSurfaceVelocityErrors(maxUerr, avUerr, maxWerr, avWerr); CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, 
       "surf vels :     maxUvec      avUvec        maxW         avW\n"); CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n", 
                  maxUerr*secpera, avUerr*secpera, maxWerr*secpera, avWerr*secpera); CHKERRQ(ierr);
  }

  // basal velocity errors if appropriate; reported in m/a except prcntavvec
  if (testname == 'E') {
    PetscScalar exactmaxspeed, maxvecerr, avvecerr, maxuberr, maxvberr;
    ierr = computeBasalVelocityErrors(exactmaxspeed,
                          maxvecerr,avvecerr,maxuberr,maxvberr); CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, 
       "base vels :  maxvector   avvector  prcntavvec     maxub     maxvb\n");
       CHKERRQ(ierr);
    ierr = verbPrintf(1,grid.com, "           %11.4f%11.5f%12.5f%10.4f%10.4f\n", 
                  maxvecerr*secpera, avvecerr*secpera, 
                  (avvecerr/exactmaxspeed)*100.0,
                  maxuberr*secpera, maxvberr*secpera); CHKERRQ(ierr);
  }

  ierr = verbPrintf(1,grid.com, "NUM ERRORS DONE\n");  CHKERRQ(ierr);
  return 0;
}

