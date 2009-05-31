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
#include <petscda.h>
#include "exactTestsFG.h" 
#include "exactTestK.h" 
#include "iceCompModel.hh"

// boundary conditions for tests F, G (same as EISMINT II Experiment F)
const PetscScalar IceCompModel::Ggeo = 0.042;
const PetscScalar IceCompModel::ST = 1.67e-5;
const PetscScalar IceCompModel::Tmin = 223.15;  // K
const PetscScalar IceCompModel::LforFG = 750000; // m
const PetscScalar IceCompModel::ApforG = 200; // m


PetscErrorCode IceCompModel::temperatureStep(PetscScalar* vertSacrCount) {
  PetscErrorCode  ierr;

  if ((testname == 'F') || (testname == 'G')) {
    ierr = Sigma3.add(1.0, SigmaComp3); CHKERRQ(ierr);	// Sigma = Sigma + Sigma_c
    ierr = IceModel::temperatureStep(vertSacrCount); CHKERRQ(ierr);
    ierr = Sigma3.add(-1.0, SigmaComp3); CHKERRQ(ierr); // Sigma = Sigma - Sigma_c
  } else {
    ierr = IceModel::temperatureStep(vertSacrCount); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode IceCompModel::initTestFG() {
  PetscErrorCode  ierr;
  PetscInt        Mz=grid.Mz;
  PetscScalar     **H, **accum, **Ts;
  PetscScalar     *dummy1, *dummy2, *dummy3, *dummy4;

  dummy1=new PetscScalar[Mz];  dummy2=new PetscScalar[Mz];
  dummy3=new PetscScalar[Mz];  dummy4=new PetscScalar[Mz];

  ierr = vbed.set(0); CHKERRQ(ierr);
  ierr = vMask.set(MASK_SHEET); CHKERRQ(ierr);
  ierr = vGhf.set(Ggeo); CHKERRQ(ierr);

  PetscScalar *T, *Tb;
  T = new PetscScalar[grid.Mz];
  Tb = new PetscScalar[grid.Mbz];

  // need pointers to surface temp and accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccTs, *pccaccum;
  ierr = atmosPCC->updateSurfTempAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccTs); CHKERRQ(ierr);  
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  ierr = pccTs->get_array(Ts); CHKERRQ(ierr);

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = T3.begin_access(); CHKERRQ(ierr);
  ierr = Tb3.begin_access(); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      Ts[i][j] = Tmin + ST * r;
      if (r > LforFG - 1.0) { // if (essentially) outside of sheet
        H[i][j] = 0.0;
        accum[i][j] = -ablationRateOutside/secpera;
        for (PetscInt k = 0; k < Mz; k++)
          T[k]=Ts[i][j];
      } else {
        r = PetscMax(r,1.0); // avoid singularity at origin
        if (testname == 'F') {
           bothexact(0.0,r,grid.zlevels,Mz,0.0,
                     &H[i][j],&accum[i][j],T,dummy1,dummy2,dummy3,dummy4);
        } else {
           bothexact(grid.year*secpera,r,grid.zlevels,Mz,ApforG,
                     &H[i][j],&accum[i][j],T,dummy1,dummy2,dummy3,dummy4);
        }
      }
      ierr = T3.setInternalColumn(i,j,T); CHKERRQ(ierr);

      // fill with basal temp increased by geothermal flux
      for (PetscInt k = 0; k < grid.Mbz; k++)
        Tb[k] = T[0] - (Ggeo / bed_thermal.k) * grid.zblevels[k];
      ierr = Tb3.setInternalColumn(i,j,Tb); CHKERRQ(ierr);
    }
  }

  ierr =     vH.end_access(); CHKERRQ(ierr);
  ierr =     T3.end_access(); CHKERRQ(ierr);
  ierr =    Tb3.end_access(); CHKERRQ(ierr);

  ierr = pccaccum->end_access(); CHKERRQ(ierr);
  ierr = pccTs->end_access(); CHKERRQ(ierr);

  ierr = vH.beginGhostComm(); CHKERRQ(ierr);
  ierr = vH.endGhostComm(); CHKERRQ(ierr);
  
  ierr = T3.beginGhostComm(); CHKERRQ(ierr);
  ierr = T3.endGhostComm(); CHKERRQ(ierr);

  ierr = vH.copy_to(vh); CHKERRQ(ierr);

  delete [] dummy1;  delete [] dummy2;  delete [] dummy3;  delete [] dummy4;
  delete [] T;  delete [] Tb;
  
  return 0;
}


PetscErrorCode IceCompModel::getCompSourcesTestFG() {
  PetscErrorCode  ierr;
  PetscInt        Mz=grid.Mz;
  PetscScalar     **accum;
  PetscScalar     dummy0;
  PetscScalar     *dummy1, *dummy2, *dummy3, *dummy4;

  // need pointer to surface accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccaccum;
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  dummy1=new PetscScalar[Mz];  dummy2=new PetscScalar[Mz];
  dummy3=new PetscScalar[Mz];  dummy4=new PetscScalar[Mz];

  PetscScalar *SigmaC;
  SigmaC = new PetscScalar[Mz];

  // before temperature and flow step, set Sigma_c and accumulation from exact values
  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  ierr = SigmaComp3.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      if (r > LforFG - 1.0) {  // outside of sheet
        accum[i][j] = -ablationRateOutside/secpera;
        ierr = SigmaComp3.setColumn(i,j,0.0); CHKERRQ(ierr);
      } else {
        r = PetscMax(r,1.0); // avoid singularity at origin
        if (testname == 'F') {
          bothexact(0.0,r,grid.zlevels,Mz,0.0,
                    &dummy0,&accum[i][j],dummy1,dummy2,dummy3,dummy4,SigmaC);
        } else {
          bothexact(grid.year*secpera,r,grid.zlevels,Mz,ApforG,
                    &dummy0,&accum[i][j],dummy1,dummy2,dummy3,dummy4,SigmaC);
        }
        for (PetscInt k=0;  k<Mz;  k++) // scale Sigma to J/(s m^3)
          SigmaC[k] = SigmaC[k] * ice->rho * ice->c_p;
        ierr = SigmaComp3.setInternalColumn(i,j,SigmaC); CHKERRQ(ierr);
      }
    }
  }
  ierr = pccaccum->end_access(); CHKERRQ(ierr);
  ierr = SigmaComp3.end_access(); CHKERRQ(ierr);

  delete [] dummy1;  delete [] dummy2;  delete [] dummy3;  delete [] dummy4;
  delete [] SigmaC;

  return 0;
}


PetscErrorCode IceCompModel::fillSolnTestFG() {
  // fills Vecs vH, vh, vAccum, T3, u3, v3, w3, Sigma3, vSigmaComp
  PetscErrorCode  ierr;
  PetscInt        Mz=grid.Mz;
  PetscScalar     **H, **accum;
  PetscScalar     Ts, *Uradial;

  // need pointer to surface accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccaccum;
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  

  Uradial = new PetscScalar[Mz];

  PetscScalar *T, *u, *v, *w, *Sigma, *SigmaC;
  T = new PetscScalar[grid.Mz];
  u = new PetscScalar[grid.Mz];
  v = new PetscScalar[grid.Mz];
  w = new PetscScalar[grid.Mz];
  Sigma = new PetscScalar[grid.Mz];
  SigmaC = new PetscScalar[grid.Mz];

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = pccaccum->get_array(accum); CHKERRQ(ierr);
  
  ierr = T3.begin_access(); CHKERRQ(ierr);
  ierr = u3.begin_access(); CHKERRQ(ierr);
  ierr = v3.begin_access(); CHKERRQ(ierr);
  ierr = w3.begin_access(); CHKERRQ(ierr);
  ierr = Sigma3.begin_access(); CHKERRQ(ierr);
  ierr = SigmaComp3.begin_access(); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      if (r > LforFG - 1.0) {  // outside of sheet
        accum[i][j] = -ablationRateOutside/secpera;
        H[i][j] = 0.0;
        Ts = Tmin + ST * r;
        ierr = T3.setColumn(i,j,Ts); CHKERRQ(ierr);
        ierr = u3.setColumn(i,j,0.0); CHKERRQ(ierr);
        ierr = v3.setColumn(i,j,0.0); CHKERRQ(ierr);
        ierr = w3.setColumn(i,j,0.0); CHKERRQ(ierr);
        ierr = Sigma3.setColumn(i,j,0.0); CHKERRQ(ierr);
        ierr = SigmaComp3.setColumn(i,j,0.0); CHKERRQ(ierr);
      } else {  // inside the sheet
        r = PetscMax(r,1.0); // avoid singularity at origin
        if (testname == 'F') {
          bothexact(0.0,r,grid.zlevels,Mz,0.0,
                    &H[i][j],&accum[i][j],T,Uradial,w, Sigma,SigmaC);
        } else {
          bothexact(grid.year*secpera,r,grid.zlevels,Mz,ApforG,
                    &H[i][j],&accum[i][j],T,Uradial,w, Sigma,SigmaC);
        }
        for (PetscInt k = 0; k < Mz; k++) {
          u[k] = Uradial[k]*(xx/r);
          v[k] = Uradial[k]*(yy/r);
          Sigma[k] = Sigma[k] * ice->rho * ice->c_p; // scale Sigma to J/(s m^3)
          SigmaC[k] = SigmaC[k] * ice->rho * ice->c_p; // scale SigmaC to J/(s m^3)
        }
        ierr = T3.setInternalColumn(i,j,T); CHKERRQ(ierr);
        ierr = u3.setInternalColumn(i,j,u); CHKERRQ(ierr);
        ierr = v3.setInternalColumn(i,j,v); CHKERRQ(ierr);
        ierr = w3.setInternalColumn(i,j,w); CHKERRQ(ierr);
        ierr = Sigma3.setInternalColumn(i,j,Sigma); CHKERRQ(ierr);
        ierr = SigmaComp3.setInternalColumn(i,j,SigmaC); CHKERRQ(ierr);
      }      
    }
  }

  ierr = T3.end_access(); CHKERRQ(ierr);
  ierr = u3.end_access(); CHKERRQ(ierr);
  ierr = v3.end_access(); CHKERRQ(ierr);
  ierr = w3.end_access(); CHKERRQ(ierr);
  ierr = Sigma3.end_access(); CHKERRQ(ierr);
  ierr = SigmaComp3.end_access(); CHKERRQ(ierr);
  
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = pccaccum->end_access(); CHKERRQ(ierr);

  delete [] Uradial;

  delete [] T;  delete [] u;  delete [] v;  delete [] w;
  delete [] Sigma;  delete [] SigmaC;

  ierr = vH.beginGhostComm(); CHKERRQ(ierr);
  ierr = vH.endGhostComm(); CHKERRQ(ierr);
  ierr = vH.copy_to(vh); CHKERRQ(ierr);

  ierr = T3.beginGhostComm(); CHKERRQ(ierr);
  ierr = u3.beginGhostComm(); CHKERRQ(ierr);
  ierr = v3.beginGhostComm(); CHKERRQ(ierr);

  ierr = T3.endGhostComm(); CHKERRQ(ierr);
  ierr = u3.endGhostComm(); CHKERRQ(ierr);
  ierr = v3.endGhostComm(); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IceCompModel::updateCompViewers() {
  PetscErrorCode ierr;

  ierr = updateViewers();  CHKERRQ(ierr);

  Vec myg2;
  ierr = DACreateGlobalVector(grid.da2, &myg2); CHKERRQ(ierr);
  IceModelVec2 myvWork2d[3];
  ierr = myvWork2d[0].create(grid, "myvWork2d[0]", true); CHKERRQ(ierr);
  ierr = myvWork2d[1].create(grid, "myvWork2d[1]", true); CHKERRQ(ierr);
  ierr = myvWork2d[2].create(grid, "myvWork2d[2]", true); CHKERRQ(ierr);
  
  if (SigmaCompView != PETSC_NULL) {
    ierr = SigmaComp3.begin_access(); CHKERRQ(ierr);
    ierr = SigmaComp3.getHorSlice(myg2, kd * grid.Mz); CHKERRQ(ierr);
    ierr = SigmaComp3.end_access(); CHKERRQ(ierr);
    ierr = VecScale(myg2, secpera); CHKERRQ(ierr);
    ierr = VecView(myg2, SigmaCompView); CHKERRQ(ierr);
  }  
  // now redraw Sigma view to be just the strain-heating, not the sum of strain-heating 
  //      and compensatory
  if (compSigmaMapView != PETSC_NULL) {
    ierr = Sigma3.begin_access(); CHKERRQ(ierr);
    ierr = SigmaComp3.begin_access(); CHKERRQ(ierr);
    ierr = Sigma3.getHorSlice(myvWork2d[0], kd * grid.Mz); CHKERRQ(ierr);
    ierr = SigmaComp3.getHorSlice(myvWork2d[1], kd * grid.Mz); CHKERRQ(ierr);
    ierr = Sigma3.end_access(); CHKERRQ(ierr);
    ierr = SigmaComp3.end_access(); CHKERRQ(ierr);
    // Work2d[2] = Sigma - SigmaComp:
    ierr = myvWork2d[0].add(-1.0, myvWork2d[1], myvWork2d[2]); CHKERRQ(ierr);
    ierr = myvWork2d[2].copy_to_global(myg2); CHKERRQ(ierr);
    ierr = VecScale(myg2, secpera); CHKERRQ(ierr);
    ierr = VecView(myg2, compSigmaMapView); CHKERRQ(ierr);
  }

  ierr = myvWork2d[0].destroy(); CHKERRQ(ierr);
  ierr = myvWork2d[1].destroy(); CHKERRQ(ierr);
  ierr = myvWork2d[2].destroy(); CHKERRQ(ierr);
  ierr = VecDestroy(myg2); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceCompModel::computeTemperatureErrors(
                                  PetscScalar &gmaxTerr, PetscScalar &gavTerr) {

  PetscErrorCode ierr;
  PetscScalar    maxTerr = 0.0, avTerr = 0.0, avcount = 0.0;
  PetscScalar    **H;
  const PetscInt Mz = grid.Mz;
  
  PetscScalar   *dummy1, *dummy2, *dummy3, *dummy4, *Tex;
  PetscScalar   junk0, junk1;
  
  Tex = new PetscScalar[Mz];
  dummy1 = new PetscScalar[Mz];  dummy2 = new PetscScalar[Mz];
  dummy3 = new PetscScalar[Mz];  dummy4 = new PetscScalar[Mz];

  PetscScalar *T;

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = T3.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; i++) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; j++) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      ierr = T3.getInternalColumn(i,j,&T); CHKERRQ(ierr);
      if ((r >= 1.0) && (r <= LforFG - 1.0)) {  // only evaluate error if inside sheet 
                                                // and not at central singularity
        switch (testname) {
          case 'F':
            bothexact(0.0,r,grid.zlevels,Mz,0.0,
                      &junk0,&junk1,Tex,dummy1,dummy2,dummy3,dummy4);
            break;
          case 'G':
            bothexact(grid.year*secpera,r,grid.zlevels,Mz,ApforG,
                      &junk0,&junk1,Tex,dummy1,dummy2,dummy3,dummy4);
            break;
          default:  SETERRQ(1,"temperature errors only computable for tests F and G\n");
        }
        const PetscInt ks = grid.kBelowHeight(H[i][j]);
        for (PetscInt k = 0; k < ks; k++) {  // only eval error if below num surface
          const PetscScalar Terr = PetscAbs(T[k] - Tex[k]);
          maxTerr = PetscMax(maxTerr,Terr);
          avcount += 1.0;
          avTerr += Terr;
        }
      }
    }
  }
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = T3.end_access(); CHKERRQ(ierr);

  delete [] Tex;
  delete [] dummy1;  delete [] dummy2;  delete [] dummy3;  delete [] dummy4;
  
  ierr = PetscGlobalMax(&maxTerr, &gmaxTerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avTerr, &gavTerr, grid.com); CHKERRQ(ierr);
  PetscScalar  gavcount;
  ierr = PetscGlobalSum(&avcount, &gavcount, grid.com); CHKERRQ(ierr);
  gavTerr = gavTerr/PetscMax(gavcount,1.0);  // avoid div by zero
  return 0;
}


PetscErrorCode IceCompModel::computeIceBedrockTemperatureErrors(
                                PetscScalar &gmaxTerr, PetscScalar &gavTerr,
                                PetscScalar &gmaxTberr, PetscScalar &gavTberr) {
  PetscErrorCode ierr;

  if (testname != 'K')
    SETERRQ(1,"ice and bedrock temperature errors only computable for test K\n");

  PetscScalar    maxTerr = 0.0, avTerr = 0.0, avcount = 0.0;
  PetscScalar    maxTberr = 0.0, avTberr = 0.0, avbcount = 0.0;
  const PetscInt    Mz = grid.Mz, Mbz = grid.Mbz;
 
  PetscScalar    *Tex, *Tbex, *T;
  Tex = new PetscScalar[Mz];  
  Tbex = new PetscScalar[Mbz];

  PetscScalar *Tb;

  for (PetscInt k = 0; k < Mz; k++) {
    ierr = exactK(grid.year * secpera, grid.zlevels[k], &Tex[k],(bedrock_is_ice_forK==PETSC_TRUE));
             CHKERRQ(ierr);
  }
  for (PetscInt k = 0; k < Mbz; k++) {
    ierr = exactK(grid.year * secpera, grid.zblevels[k], &Tbex[k],(bedrock_is_ice_forK==PETSC_TRUE));
             CHKERRQ(ierr);
  }
    
  ierr = T3.begin_access(); CHKERRQ(ierr);
  ierr = Tb3.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; i++) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; j++) {
      ierr = Tb3.getInternalColumn(i,j,&Tb); CHKERRQ(ierr);
      for (PetscInt kb = 0; kb < Mbz; kb++) { 
        const PetscScalar Tberr = PetscAbs(Tb[kb] - Tbex[kb]);
        maxTberr = PetscMax(maxTberr,Tberr);
        avbcount += 1.0;
        avTberr += Tberr;
      }
      ierr = T3.getInternalColumn(i,j,&T); CHKERRQ(ierr);
      for (PetscInt k = 0; k < Mz; k++) { 
        const PetscScalar Terr = PetscAbs(T[k] - Tex[k]);
        maxTerr = PetscMax(maxTerr,Terr);
        avcount += 1.0;
        avTerr += Terr;
      }
    }
  }
  ierr = T3.end_access(); CHKERRQ(ierr);
  ierr = Tb3.end_access(); CHKERRQ(ierr);

  delete [] Tex;  delete [] Tbex;
  
  ierr = PetscGlobalMax(&maxTerr, &gmaxTerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avTerr, &gavTerr, grid.com); CHKERRQ(ierr);
  PetscScalar  gavcount;
  ierr = PetscGlobalSum(&avcount, &gavcount, grid.com); CHKERRQ(ierr);
  gavTerr = gavTerr/PetscMax(gavcount,1.0);  // avoid div by zero

  ierr = PetscGlobalMax(&maxTberr, &gmaxTberr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avTberr, &gavTberr, grid.com); CHKERRQ(ierr);
  PetscScalar  gavbcount;
  ierr = PetscGlobalSum(&avbcount, &gavbcount, grid.com); CHKERRQ(ierr);
  gavTberr = gavTberr/PetscMax(gavbcount,1.0);  // avoid div by zero
  return 0;
}


PetscErrorCode IceCompModel::computeBasalTemperatureErrors(
      PetscScalar &gmaxTerr, PetscScalar &gavTerr, PetscScalar &centerTerr) {

  PetscErrorCode  ierr;
  PetscScalar     domeT, domeTexact, Terr, avTerr;

  PetscScalar     dummy, z, Texact, dummy1, dummy2, dummy3, dummy4, dummy5;

  ierr = T3.begin_access(); CHKERRQ(ierr);

  domeT=0; domeTexact = 0; Terr=0; avTerr=0;

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      switch (testname) {
        case 'F':
          if (r > LforFG - 1.0) {  // outside of sheet
            Texact=Tmin + ST * r;  // = Ts
          } else {
            r=PetscMax(r,1.0);
            z=0.0;
            bothexact(0.0,r,&z,1,0.0,
                      &dummy5,&dummy,&Texact,&dummy1,&dummy2,&dummy3,&dummy4);
          }
          break;
        case 'G':
          if (r > LforFG -1.0) {  // outside of sheet
            Texact=Tmin + ST * r;  // = Ts
          } else {
            r=PetscMax(r,1.0);
            z=0.0;
            bothexact(grid.year*secpera,r,&z,1,ApforG,
                      &dummy5,&dummy,&Texact,&dummy1,&dummy2,&dummy3,&dummy4);
          }
          break;
        default:  SETERRQ(1,"temperature errors only computable for tests F and G\n");
      }

      const PetscScalar Tbase = T3.getValZ(i,j,0.0);
      if (i == (grid.Mx - 1)/2 && j == (grid.My - 1)/2) {
        domeT = Tbase;
        domeTexact = Texact;
      }
      // compute maximum errors
      Terr = PetscMax(Terr,PetscAbsReal(Tbase - Texact));
      // add to sums for average errors
      avTerr += PetscAbs(Tbase - Texact);
    }
  }
  ierr = T3.end_access(); CHKERRQ(ierr);
  
  PetscScalar gdomeT, gdomeTexact;

  ierr = PetscGlobalMax(&Terr, &gmaxTerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avTerr, &gavTerr, grid.com); CHKERRQ(ierr);
  gavTerr = gavTerr/(grid.Mx*grid.My);
  ierr = PetscGlobalMax(&domeT, &gdomeT, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&domeTexact, &gdomeTexact, grid.com); CHKERRQ(ierr);  
  centerTerr = PetscAbsReal(gdomeT - gdomeTexact);
  
  return 0;
}


PetscErrorCode IceCompModel::computeSigmaErrors(
                      PetscScalar &gmaxSigmaerr, PetscScalar &gavSigmaerr) {

  PetscErrorCode ierr;
  PetscScalar    maxSigerr = 0.0, avSigerr = 0.0, avcount = 0.0;
  PetscScalar    **H;
  const PetscInt Mz = grid.Mz;
  
  PetscScalar   *dummy1, *dummy2, *dummy3, *dummy4, *Sigex;
  PetscScalar   junk0, junk1;
  
  Sigex = new PetscScalar[Mz];
  dummy1 = new PetscScalar[Mz];  dummy2 = new PetscScalar[Mz];
  dummy3 = new PetscScalar[Mz];  dummy4 = new PetscScalar[Mz];

  PetscScalar *Sig;

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = Sigma3.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; i++) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; j++) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      if ((r >= 1.0) && (r <= LforFG - 1.0)) {  // only evaluate error if inside sheet 
                                                // and not at central singularity
        switch (testname) {
          case 'F':
            bothexact(0.0,r,grid.zlevels,Mz,0.0,
                      &junk0,&junk1,dummy1,dummy2,dummy3,Sigex,dummy4);
            break;
          case 'G':
            bothexact(grid.year*secpera,r,grid.zlevels,Mz,ApforG,
                      &junk0,&junk1,dummy1,dummy2,dummy3,Sigex,dummy4);
            break;
          default:
            SETERRQ(1,"strain-heating (Sigma) errors only computable for tests F and G\n");
        }
        for (PetscInt k = 0; k < Mz; k++)
          Sigex[k] = Sigex[k] * ice->rho * ice->c_p; // scale exact Sigma to J/(s m^3)
        const PetscInt ks = grid.kBelowHeight(H[i][j]);
        ierr = Sigma3.getInternalColumn(i,j,&Sig); CHKERRQ(ierr);
        for (PetscInt k = 0; k < ks; k++) {  // only eval error if below num surface
          const PetscScalar Sigerr = PetscAbs(Sig[k] - Sigex[k]);
          maxSigerr = PetscMax(maxSigerr,Sigerr);
          avcount += 1.0;
          avSigerr += Sigerr;
        }
      }
    }
  }
  ierr =     vH.end_access(); CHKERRQ(ierr);
  ierr = Sigma3.end_access(); CHKERRQ(ierr);

  delete [] Sigex;
  delete [] dummy1;  delete [] dummy2;  delete [] dummy3;  delete [] dummy4;
  
  ierr = PetscGlobalMax(&maxSigerr, &gmaxSigmaerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avSigerr, &gavSigmaerr, grid.com); CHKERRQ(ierr);
  PetscScalar  gavcount;
  ierr = PetscGlobalSum(&avcount, &gavcount, grid.com); CHKERRQ(ierr);
  gavSigmaerr = gavSigmaerr/PetscMax(gavcount,1.0);  // avoid div by zero
  return 0;
}


PetscErrorCode IceCompModel::computeSurfaceVelocityErrors(
        PetscScalar &gmaxUerr, PetscScalar &gavUerr,
        PetscScalar &gmaxWerr, PetscScalar &gavWerr) {

  PetscErrorCode ierr;
  PetscScalar    maxUerr = 0.0, maxWerr = 0.0, avUerr = 0.0, avWerr = 0.0;
  PetscScalar    **H;

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = u3.begin_access(); CHKERRQ(ierr);
  ierr = v3.begin_access(); CHKERRQ(ierr);
  ierr = w3.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; i++) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; j++) {
      PetscScalar r,xx,yy;
      mapcoords(i,j,xx,yy,r);
      if ((r >= 1.0) && (r <= LforFG - 1.0)) {  // only evaluate error if inside sheet 
                                                // and not at central singularity
        PetscScalar radialUex,wex;
        PetscScalar dummy0,dummy1,dummy2,dummy3,dummy4;
        switch (testname) {
          case 'F':
            bothexact(0.0,r,&(H[i][j]),1,0.0,
                      &dummy0,&dummy1,&dummy2,&radialUex,&wex,&dummy3,&dummy4);
            break;
          case 'G':
            bothexact(grid.year*secpera,r,&(H[i][j]),1,ApforG,
                      &dummy0,&dummy1,&dummy2,&radialUex,&wex,&dummy3,&dummy4);
            break;
          default:  SETERRQ(1,"surface velocity errors only computed for tests F and G\n");
        }
        const PetscScalar uex = (xx/r) * radialUex;
        const PetscScalar vex = (yy/r) * radialUex;
        // note that because getValZ does linear interpolation and H[i][j] is not exactly at
        // a grid point, this causes nonzero errors even with option -eo
        const PetscScalar Uerr = sqrt(PetscSqr(u3.getValZ(i,j,H[i][j]) - uex)
                                      + PetscSqr(v3.getValZ(i,j,H[i][j]) - vex));
        maxUerr = PetscMax(maxUerr,Uerr);
        avUerr += Uerr;
        const PetscScalar Werr = PetscAbs(w3.getValZ(i,j,H[i][j]) - wex);
        maxWerr = PetscMax(maxWerr,Werr);
        avWerr += Werr;
      }
    }
  }
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = u3.end_access(); CHKERRQ(ierr);
  ierr = v3.end_access(); CHKERRQ(ierr);
  ierr = w3.end_access(); CHKERRQ(ierr);

  ierr = PetscGlobalMax(&maxUerr, &gmaxUerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&maxWerr, &gmaxWerr, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avUerr, &gavUerr, grid.com); CHKERRQ(ierr);
  gavUerr = gavUerr/(grid.Mx*grid.My);
  ierr = PetscGlobalSum(&avWerr, &gavWerr, grid.com); CHKERRQ(ierr);
  gavWerr = gavWerr/(grid.Mx*grid.My);
  return 0;
}


PetscErrorCode IceCompModel::fillSolnTestK() {
  PetscErrorCode    ierr;
  const PetscInt    Mz = grid.Mz, 
                    Mbz = grid.Mbz; 

  PetscScalar       *Tcol, *Tbcol;
  Tcol = new PetscScalar[Mz];
  Tbcol = new PetscScalar[Mbz];

  // evaluate exact solution in a column; all columns are the same
  for (PetscInt k=0; k<Mz; k++) {
    ierr = exactK(grid.year * secpera, grid.zlevels[k], &Tcol[k],(bedrock_is_ice_forK==PETSC_TRUE));
             CHKERRQ(ierr);
  }
  for (PetscInt k=0; k<Mbz; k++) {
    if (exactK(grid.year * secpera, grid.zblevels[k], &Tbcol[k],(bedrock_is_ice_forK==PETSC_TRUE)) != 0)
      SETERRQ1(1,"exactK() reports that level %9.7f is below B0 = -1000.0 m\n", grid.zblevels[k]);
  }

  // copy column values into 3D arrays
  ierr = T3.begin_access(); CHKERRQ(ierr);
  ierr = Tb3.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      ierr = T3.setInternalColumn(i,j,Tcol); CHKERRQ(ierr);
      ierr = Tb3.setInternalColumn(i,j,Tbcol); CHKERRQ(ierr);
    }
  }
  ierr = T3.end_access(); CHKERRQ(ierr);
  ierr = Tb3.end_access(); CHKERRQ(ierr);

  delete [] Tcol;  delete [] Tbcol;

  // only communicate T (as Tb will not be horizontally differentiated)
  ierr = T3.beginGhostComm(); CHKERRQ(ierr);
  ierr = T3.endGhostComm(); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceCompModel::initTestK() {
  PetscErrorCode    ierr;


  // need pointers to surface temp and accum, from PISMAtmosphereCoupler atmosPCC*
  IceModelVec2  *pccTs, *pccaccum;
  ierr = atmosPCC->updateSurfTempAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccTs); CHKERRQ(ierr);  
  ierr = atmosPCC->updateSurfMassFluxAndProvide(grid.year, 0.0, // year and dt irrelevant here 
                  (void*)(&info_atmoscoupler), pccaccum); CHKERRQ(ierr);  
  
  ierr = pccaccum->set(0.0); CHKERRQ(ierr);
  ierr = pccTs->set(223.15); CHKERRQ(ierr);

  ierr = vbed.set(0.0); CHKERRQ(ierr);
  ierr = vMask.set(MASK_SHEET); CHKERRQ(ierr);
  ierr = vGhf.set(0.042); CHKERRQ(ierr);
  ierr = vH.set(3000.0); CHKERRQ(ierr);
  ierr = vHmelt.set(0.0); CHKERRQ(ierr);
  ierr = tau3.set(0.0); CHKERRQ(ierr);
  ierr = vH.copy_to(vh); CHKERRQ(ierr);

  ierr = fillSolnTestK(); CHKERRQ(ierr);
  return 0;
}

