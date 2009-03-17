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

#include "materials.hh"
#include "pism_const.hh"

IceType::IceType(MPI_Comm c,const char pre[]) : comm(c) {
  PetscMemzero(prefix,sizeof(prefix));
  if (pre) PetscStrncpy(prefix,pre,sizeof(prefix));

  rho    = 910;          // kg/m^3       density
  beta_CC_grad = 8.66e-4;// K/m          Clausius-Clapeyron gradient
  k      = 2.10;         // J/(m K s) = W/(m K)    thermal conductivity
  c_p    = 2009;         // J/(kg K)     specific heat capacity
  latentHeat = 3.35e5;   // J/kg         latent heat capacity
  meltingTemp = 273.15;  // K            melting point
}


PetscErrorCode IceType::printInfo(PetscInt) const {
  PetscPrintf(comm,"WARNING:  IceType::printInfo() called but base class IceType is virtual!!\n");
  return 0;
}


// Rather than make this part of the base class, we just check at some reference values.
PetscTruth IceTypeIsPatersonBuddCold(IceType *ice) {
  static const struct {PetscReal s,T,p,gs;} v[] = {
    {1e3,223,1e6,1e-3},{2e4,254,3e6,2e-3},{5e4,268,5e6,3e-3},{1e5,273,8e6,5e-3}};
  ThermoGlenArrIce cpb(PETSC_COMM_SELF,NULL); // This is unmodified cold Paterson-Budd
  for (int i=0; i<4; i++) {
    const PetscReal left  = ice->flow(v[i].s, v[i].T, v[i].p, v[i].gs),
                    right =  cpb.flow(v[i].s, v[i].T, v[i].p, v[i].gs);
/* very strange result on bueler-laptop, r588:
      PetscPrintf(PETSC_COMM_WORLD,
            "IceTypeIsPatersonBuddCold case %d fails: (left - right)/left = %12.10e\n",
            i,(left - right)/left);
IceTypeIsPatersonBuddCold case 0 fails: (left - right)/left = 1.7629198155e-17
IceTypeIsPatersonBuddCold case 1 fails: (left - right)/left = -7.0007174077e-18
IceTypeIsPatersonBuddCold case 2 fails: (left - right)/left = 9.5606965946e-17
IceTypeIsPatersonBuddCold case 3 fails: (left - right)/left = -4.5626346464e-17
80 bit register effect?
so equality test
    "if (left != right) {"
is changed to 15 digit test below
*/
    if (PetscAbs((left - right)/left)>1.0e-15) {
      return PETSC_FALSE;
    }
  }
  return PETSC_TRUE;
}


PetscTruth IceTypeUsesGrainSize(IceType *ice) {
  static const PetscReal gs[] = {1e-4,1e-3,1e-2,1},s=1e4,T=260,p=1e6;
  PetscReal ref = ice->flow(s,T,p,gs[0]);
  for (int i=1; i<4; i++) {
    if (ice->flow(s,T,p,gs[i]) != ref) return PETSC_TRUE;
  }
  return PETSC_FALSE;
}




CustomGlenIce::CustomGlenIce(MPI_Comm c,const char *pre) : IceType(c,pre)
{
  exponent_n = 3.0;
  softness_A = 4e-25;
  hardness_B = pow(softness_A, -1/exponent_n); // ~= 135720960;
  setSchoofRegularization(1,1000);             // Units of km
}


PetscScalar CustomGlenIce::flow(PetscScalar stress,PetscScalar,PetscScalar,PetscScalar) const
{
  return softness_A * pow(stress,exponent_n-1);
}


PetscScalar CustomGlenIce::effectiveViscosityColumn(PetscScalar H, PetscInt, const PetscScalar *,
                           PetscScalar u_x, PetscScalar u_y, PetscScalar v_x, PetscScalar v_y,
                           const PetscScalar *, const PetscScalar *) const  {
  return H * (hardness_B / 2) * pow(schoofReg + secondInvariant(u_x,u_y,v_x,v_y), (1-exponent_n)/(2*exponent_n));
}


PetscInt CustomGlenIce::integratedStoreSize() const { return 1; }


void CustomGlenIce::integratedStore(PetscScalar H,PetscInt,const PetscScalar*,const PetscScalar[],PetscScalar store[]) const
{
  store[0] = H * hardness_B / 2;
}


void CustomGlenIce::integratedViscosity(const PetscScalar store[], const PetscScalar Du[], PetscScalar *eta, PetscScalar *deta) const
{
  const PetscScalar alpha = secondInvariantDu(Du),power = (1-exponent_n) / (2*exponent_n);
  *eta = store[0] * pow(schoofReg + alpha, power);
  if (deta) *deta = power * *eta / (schoofReg + alpha);
}


PetscErrorCode CustomGlenIce::setDensity(PetscReal r) {rho = r; return 0;}


PetscErrorCode CustomGlenIce::setExponent(PetscReal n) {exponent_n = n; return 0;}


PetscErrorCode CustomGlenIce::setSchoofRegularization(PetscReal vel,PetscReal len) {
  schoofVel = vel/secpera;  // vel has units m/a
  schoofLen = len*1e3;      // len has units km
  schoofReg = PetscSqr(schoofVel/schoofLen); 
  return 0;
}


PetscErrorCode CustomGlenIce::setSoftness(PetscReal A) {
  softness_A = A;
  hardness_B = pow(A,-1/exponent_n); 
  return 0;
}


PetscErrorCode CustomGlenIce::setHardness(PetscReal B) {
  hardness_B = B;
  softness_A = pow(B,-exponent_n);
  return 0;
}


PetscScalar CustomGlenIce::exponent() const { return exponent_n; }


PetscScalar CustomGlenIce::softnessParameter(PetscScalar T) const { return softness_A; }


PetscScalar CustomGlenIce::hardnessParameter(PetscScalar T) const { return hardness_B; }


PetscErrorCode CustomGlenIce::setFromOptions()
{
  PetscReal      n = exponent_n,B=hardness_B,A=softness_A,slen=schoofLen/1e3,svel=schoofVel*secpera;
  PetscTruth     flg;
  PetscErrorCode ierr;

  ierr = PetscOptionsBegin(comm,prefix,"CustomGlenIce options",NULL);CHKERRQ(ierr);
  {
    ierr = PetscOptionsReal("-ice_custom_n","Power-law exponent","setExponent",n,&n,&flg);CHKERRQ(ierr);
    if (flg) {ierr = setExponent(n);CHKERRQ(ierr);}
    ierr = PetscOptionsReal("-ice_custom_softness","Softness parameter A (Pa^{-n} s^{-1})","setSoftness",A,&A,&flg);CHKERRQ(ierr);
    if (flg) {ierr = setSoftness(A);CHKERRQ(ierr);}
    ierr = PetscOptionsReal("-ice_custom_hardness","Hardness parameter B (Pa s^{1/n})","setHardness",B,&B,&flg);CHKERRQ(ierr);
    if (flg) {ierr = setHardness(B);CHKERRQ(ierr);}
    ierr = PetscOptionsReal("-ice_custom_density","Density rho (km m^{-1})","setDensity",rho,&rho,NULL);CHKERRQ(ierr);
    // use -ice_ instead of -ice_custom_ to be compatible with ThermoGlenIce
    ierr = PetscOptionsReal("-ice_reg_schoof_vel","Regularizing velocity (Schoof definition, m/a)","setSchoofRegularization",svel,&svel,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ice_reg_schoof_length","Regularizing length (Schoof definition, km)","setSchoofRegularization",slen,&slen,NULL);CHKERRQ(ierr);
    ierr = setSchoofRegularization(svel,slen);CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  return 0;
}

PetscErrorCode CustomGlenIce::printInfo(PetscInt thresh) const {
  PetscErrorCode ierr;
  if (thresh <= getVerbosityLevel()) {
    ierr = view(NULL);CHKERRQ(ierr);
  }
  return 0;
}

PetscErrorCode CustomGlenIce::view(PetscViewer viewer) const {
  PetscErrorCode ierr;
  PetscTruth iascii;

  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(comm,&viewer);CHKERRQ(ierr);
  }
  ierr = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&iascii);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"CustomGlenIce object (%s)\n",prefix);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,
        "  n=%3g,   A=%8.3e,   B=%8.3e,  rho=%.1f,\n"
        "  v_schoof=%.2f m/a,   L_schoof=%.2f km,  schoofReg = %.2e\n",
        exponent_n,softness_A,hardness_B,rho,schoofVel*secpera,schoofLen/1e3,schoofReg);CHKERRQ(ierr);
  } else {
    SETERRQ(1,"No binary viewer for this object\n");
  }
  return 0;
}


ThermoGlenIce::ThermoGlenIce(MPI_Comm c,const char pre[]) : IceType(c,pre) {
  A_cold = 3.61e-13;   // Pa^-3 / s
  A_warm = 1.73e3;     // Pa^-3 / s
  Q_cold = 6.0e4;      // J / mol
  Q_warm = 13.9e4;     // J / mol
  crit_temp = 263.15;  // K
  n = 3;
  schoofLen = 1e6;
  schoofVel = 1/secpera;
  schoofReg = PetscSqr(schoofVel/schoofLen);
}


PetscErrorCode ThermoGlenIce::setFromOptions() {
  PetscErrorCode ierr;
  PetscReal slen=schoofLen/1e3,svel=schoofVel*secpera;

  ierr = PetscOptionsBegin(comm,prefix,"ThermoGlenIce options",NULL);CHKERRQ(ierr);
  {
    ierr = PetscOptionsReal("-ice_reg_schoof_vel","Regularizing velocity (Schoof definition, m/a)","",svel,&svel,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ice_reg_schoof_length","Regularizing length (Schoof definition, km)","",slen,&slen,NULL);CHKERRQ(ierr);
    schoofVel = svel / secpera;
    schoofLen = slen * 1e3;
    schoofReg = PetscSqr(schoofVel/schoofLen);
    ierr = PetscOptionsReal("-ice_pb_A_cold","Paterson-Budd cold softness parameter (Pa^-3 s^-1)","",A_cold,&A_cold,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ice_pb_A_warm","Paterson-Budd warm softness parameter (Pa^-3 s^-1)","",A_warm,&A_warm,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ice_pb_Q_cold","Paterson-Budd activation energy (J/mol)","",Q_cold,&Q_cold,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ice_pb_Q_warm","Paterson-Budd activation energy (J/mol)","",Q_warm,&Q_warm,NULL);CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  return 0;
}


PetscErrorCode ThermoGlenIce::printInfo(PetscInt thresh) const {
  PetscErrorCode ierr;
  if (thresh <= getVerbosityLevel()) {
    ierr = view(NULL);CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode ThermoGlenIce::view(PetscViewer viewer) const {
  PetscErrorCode ierr;
  PetscTruth iascii;

  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(comm,&viewer);CHKERRQ(ierr);
  }
  ierr = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&iascii);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"ThermoGlenIce object (%s)\n",prefix);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  v_schoof=%4f m/a L_schoof=%4f km\n",schoofVel*secpera,schoofLen/1e3);CHKERRQ(ierr);
  } else {
    SETERRQ(1,"No binary viewer for this object\n");
  }
  return 0;
}


PetscScalar ThermoGlenIce::flow(PetscScalar stress,PetscScalar temp,PetscScalar pressure,PetscScalar) const {
  const PetscScalar T = temp + (beta_CC_grad / (rho * earth_grav)) * pressure; // homologous temp
  return softnessParameter(T) * pow(stress,n-1);
}


PetscScalar ThermoGlenIce::effectiveViscosityColumn(
                PetscScalar H,  PetscInt kbelowH, const PetscScalar *zlevels,
                PetscScalar u_x,  PetscScalar u_y, PetscScalar v_x,  PetscScalar v_y,
                const PetscScalar *T1, const PetscScalar *T2) const {
  // DESPITE NAME, does *not* return effective viscosity.
  // The result is \nu_e H, i.e. viscosity times thickness.
  // B is really hardness times thickness.
//  const PetscInt  ks = static_cast<PetscInt>(floor(H/dz));
  // Integrate the hardness parameter using the trapezoid rule.
  PetscScalar B = 0;
  if (kbelowH > 0) {
    PetscScalar dz = zlevels[1] - zlevels[0];
    B += 0.5 * dz * hardnessParameter(0.5 * (T1[0] + T2[0]) + beta_CC_grad * H);
    for (PetscInt m=1; m < kbelowH; m++) {
      const PetscScalar dzNEXT = zlevels[m+1] - zlevels[m];
      B += 0.5 * (dz + dzNEXT) * hardnessParameter(0.5 * (T1[m] + T2[m])
           + beta_CC_grad * (H - zlevels[m]));
      dz = dzNEXT;
    }
    // use last dz
    B += 0.5 * dz * hardnessParameter(0.5 * (T1[kbelowH] + T2[kbelowH])
                                      + beta_CC_grad * (H - zlevels[kbelowH]));
  }
  const PetscScalar alpha = secondInvariant(u_x, u_y, v_x, v_y);
  return 0.5 * B * pow(schoofReg + alpha, (1-n)/(2*n));
}


PetscInt ThermoGlenIce::integratedStoreSize() const {return 1;}


void ThermoGlenIce::integratedStore(PetscScalar H, PetscInt kbelowH, const PetscScalar *zlevels,
                              const PetscScalar T[], PetscScalar store[]) const
{
  PetscScalar B = 0;
  if (kbelowH > 0) {
    PetscScalar dz = zlevels[1] - zlevels[0];
    B += 0.5 * dz * hardnessParameter(T[0] + beta_CC_grad * H);
    for (PetscInt m=1; m < kbelowH; m++) {
      const PetscScalar dzNEXT = zlevels[m+1] - zlevels[m];
      B += 0.5 * (dz + dzNEXT) * hardnessParameter(T[m]
           + beta_CC_grad * (H - zlevels[m]));
      dz = dzNEXT;
    }
    // use last dz
    B += 0.5 * dz * hardnessParameter(T[kbelowH] + beta_CC_grad * (H - zlevels[kbelowH]));
  }
  store[0] = B / 2;
}


void ThermoGlenIce::integratedViscosity(const PetscScalar store[],const PetscScalar Du[], PetscScalar *eta, PetscScalar *deta) const
{
  const PetscScalar alpha = secondInvariantDu(Du);
  *eta = store[0] * pow(schoofReg + alpha, (1-n)/(2*n));
  if (deta) *deta = (1-n)/(2*n) * *eta / (schoofReg + alpha);
}


PetscScalar ThermoGlenIce::exponent() const { return n; }


//! Return the softness parameter A(T) for a given temperature T.
PetscScalar ThermoGlenIce::softnessParameter(PetscScalar T) const {
  if (T < crit_temp) {
    return A_cold * exp(-Q_cold/(gasConst_R * T));
  }
  return A_warm * exp(-Q_warm/(gasConst_R * T));
}


//! Return the hardness parameter A(T) for a given temperature T.
PetscScalar ThermoGlenIce::hardnessParameter(PetscScalar T) const {
  return pow(softnessParameter(T), -1.0/n);
}


ThermoGlenIceHooke::ThermoGlenIceHooke(MPI_Comm c,const char pre[]) : ThermoGlenIce(c,pre) {
  Q_Hooke = 7.88e4;       // J / mol
  // A_Hooke = (1/B_0)^n where n=3 and B_0 = 1.928 a^(1/3) Pa
  A_Hooke = 4.42165e-9;    // s^-1 Pa^-3
  C_Hooke = 0.16612;       // Kelvin^K_Hooke
  K_Hooke = 1.17;          // [unitless]
  Tr_Hooke = 273.39;       // Kelvin
  R_Hooke = 8.321;         // J mol^-1 K^-1
}


PetscScalar ThermoGlenIceHooke::softnessParameter(PetscScalar T) const {

  return A_Hooke * exp( -Q_Hooke/(R_Hooke * T)
                       + 3.0 * C_Hooke * pow(Tr_Hooke - T,-K_Hooke));
}


PetscErrorCode ThermoGlenArrIce::view(PetscViewer viewer) const {
  PetscErrorCode ierr;
  PetscTruth iascii;

  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(comm,&viewer);CHKERRQ(ierr);
  }
  ierr = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&iascii);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"ThermoGlenArrIce object (%s)\n",prefix);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  No customizable options specific to ThermoGlenArrIce\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  Derived from ThermoGlenIce with the following state\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
    ierr = ThermoGlenIce::view(viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
  } else {
    SETERRQ(1,"No binary viewer for this object\n");
  }
  return 0;
}


//! Return the softness parameter A(T) for a given temperature T.
PetscScalar ThermoGlenArrIce::softnessParameter(PetscScalar T) const {
  return A() * exp(-Q()/(gasConst_R * T));
}


//! Return the temperature T corresponding to a given value A=A(T).
PetscScalar ThermoGlenArrIce::tempFromSoftness(PetscScalar myA) const {
  return - Q() / (gasConst_R * (log(myA) - log(A())));
}


PetscScalar ThermoGlenArrIce::flow(PetscScalar stress, PetscScalar temp, PetscScalar,PetscScalar) const {
  // ignores pressure
  return softnessParameter(temp) * pow(stress,n-1);  // uses NON-homologous temp
}

PetscScalar ThermoGlenArrIce::A() const {
  return A_cold;
}

PetscScalar ThermoGlenArrIce::Q() const {
  return Q_cold;
}


PetscErrorCode ThermoGlenArrIceWarm::view(PetscViewer viewer) const {
  PetscErrorCode ierr;
  PetscTruth iascii;

  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(comm,&viewer);CHKERRQ(ierr);
  }
  ierr = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&iascii);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"ThermoGlenArrIceWarm object (%s)\n",prefix);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  No customizable options specific to ThermoGlenArrIceWarm\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  Derived from ThermoGlenArrIce with the following state\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
    ierr = ThermoGlenArrIce::view(viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
  } else {
    SETERRQ(1,"No binary viewer for this object\n");
  }
  return 0;
}

PetscScalar ThermoGlenArrIceWarm::A() const {
  return A_warm;
}

PetscScalar ThermoGlenArrIceWarm::Q() const {
  return Q_warm;
}


HybridIce::HybridIce(MPI_Comm c,const char pre[]) : ThermoGlenIce(c,pre) {
  V_act_vol    = -13.e-6;  // m^3/mol
  d_grain_size = 1.0e-3;   // m  (see p. ?? of G&K paper)
  //--- dislocation creep ---
  disl_crit_temp=258.0,    // Kelvin
  //disl_A_cold=4.0e5,                  // MPa^{-4.0} s^{-1}
  //disl_A_warm=6.0e28,                 // MPa^{-4.0} s^{-1}
  disl_A_cold=4.0e-19,     // Pa^{-4.0} s^{-1}
  disl_A_warm=6.0e4,       // Pa^{-4.0} s^{-1} (GK)
  disl_n=4.0,              // stress exponent
  disl_Q_cold=60.e3,       // J/mol Activation energy
  disl_Q_warm=180.e3;      // J/mol Activation energy (GK)
  //--- grain boundary sliding ---
  gbs_crit_temp=255.0,     // Kelvin
  //gbs_A_cold=3.9e-3,                  // MPa^{-1.8} m^{1.4} s^{-1}
  //gbs_A_warm=3.e26,                   // MPa^{-1.8} m^{1.4} s^{-1}
  gbs_A_cold=6.1811e-14,   // Pa^{-1.8} m^{1.4} s^{-1}
  gbs_A_warm=4.7547e15,    // Pa^{-1.8} m^{1.4} s^{-1}
  gbs_n=1.8,               // stress exponent
  gbs_Q_cold=49.e3,        // J/mol Activation energy
  gbs_Q_warm=192.e3,       // J/mol Activation energy
  p_grain_sz_exp=1.4;      // from Peltier
  //--- easy slip (basal) ---
  //basal_A=5.5e7,                      // MPa^{-2.4} s^{-1}
  basal_A=2.1896e-7,       // Pa^{-2.4} s^{-1}
  basal_n=2.4,             // stress exponent
  basal_Q=60.e3;           // J/mol Activation energy
  //--- diffusional flow ---
  diff_crit_temp=258.0,    // when to use enhancement factor
  diff_V_m=1.97e-5,        // Molar volume (m^3/mol)
  diff_D_0v=9.10e-4,       // Preexponential volume diffusion (m^2/s)
  diff_Q_v=59.4e3,         // activation energy, vol. diff. (J/mol)
  diff_D_0b=5.8e-4,        // preexponential grain boundary coeff.
  diff_Q_b=49.e3,          // activation energy, g.b. (J/mol)
  diff_delta=9.04e-10;     // grain boundary width (m)
}


PetscScalar HybridIce::flow(PetscScalar stress, PetscScalar temp,
                            PetscScalar pressure, PetscScalar gs) const {
  /*
  This is the (forward) Goldsby-Kohlstedt flow law.  See:
  D. L. Goldsby & D. L. Kohlstedt (2001), "Superplastic deformation
  of ice: experimental observations", J. Geophys. Res. 106(M6), 11017-11030.
  */
  PetscScalar eps_diff, eps_disl, eps_basal, eps_gbs, diff_D_b;

  if (PetscAbs(stress) < 1e-10) return 0;
  const PetscScalar T = temp + (beta_CC_grad / (rho * earth_grav)) * pressure;
  const PetscScalar pV = pressure * V_act_vol;
  const PetscScalar RT = gasConst_R * T;
  // Diffusional Flow
  const PetscScalar diff_D_v = diff_D_0v * exp(-diff_Q_v/RT);
  diff_D_b = diff_D_0b * exp(-diff_Q_b/RT);
  if (T > diff_crit_temp) diff_D_b *= 1000; // Coble creep scaling
  eps_diff = 14 * diff_V_m *
    (diff_D_v + M_PI * diff_delta * diff_D_b / gs) / (RT*PetscSqr(gs));
  // Dislocation Creep
  if (T > disl_crit_temp)
    eps_disl = disl_A_warm * pow(stress, disl_n-1) * exp(-(disl_Q_warm + pV)/RT);
  else
    eps_disl = disl_A_cold * pow(stress, disl_n-1) * exp(-(disl_Q_cold + pV)/RT);
  // Basal Slip
  eps_basal = basal_A * pow(stress, basal_n-1) * exp(-(basal_Q + pV)/RT);
  // Grain Boundary Sliding
  if (T > gbs_crit_temp)
    eps_gbs = gbs_A_warm * (pow(stress, gbs_n-1) / pow(gs, p_grain_sz_exp)) *
      exp(-(gbs_Q_warm + pV)/RT);
  else
    eps_gbs = gbs_A_cold * (pow(stress, gbs_n-1) / pow(gs, p_grain_sz_exp)) *
      exp(-(gbs_Q_cold + pV)/RT);

  return eps_diff + eps_disl + (eps_basal * eps_gbs) / (eps_basal + eps_gbs);
}

/*****************
THE NEXT PROCEDURE REPEATS CODE; INTENDED ONLY FOR DEBUGGING
*****************/
GKparts HybridIce::flowParts(PetscScalar stress,PetscScalar temp,PetscScalar pressure) const {
  PetscScalar gs, eps_diff, eps_disl, eps_basal, eps_gbs, diff_D_b;
  GKparts p;

  if (PetscAbs(stress) < 1e-10) {
    p.eps_total=0.0;
    p.eps_diff=0.0; p.eps_disl=0.0; p.eps_gbs=0.0; p.eps_basal=0.0;
    return p;
  }
  const PetscScalar T = temp + (beta_CC_grad / (rho * earth_grav)) * pressure;
  const PetscScalar pV = pressure * V_act_vol;
  const PetscScalar RT = gasConst_R * T;
  // Diffusional Flow
  const PetscScalar diff_D_v = diff_D_0v * exp(-diff_Q_v/RT);
  diff_D_b = diff_D_0b * exp(-diff_Q_b/RT);
  if (T > diff_crit_temp) diff_D_b *= 1000; // Coble creep scaling
  gs = d_grain_size;
  eps_diff = 14 * diff_V_m *
    (diff_D_v + M_PI * diff_delta * diff_D_b / gs) / (RT*PetscSqr(gs));
  // Dislocation Creep
  if (T > disl_crit_temp)
    eps_disl = disl_A_warm * pow(stress, disl_n-1) * exp(-(disl_Q_warm + pV)/RT);
  else
    eps_disl = disl_A_cold * pow(stress, disl_n-1) * exp(-(disl_Q_cold + pV)/RT);
  // Basal Slip
  eps_basal = basal_A * pow(stress, basal_n-1) * exp(-(basal_Q + pV)/RT);
  // Grain Boundary Sliding
  if (T > gbs_crit_temp)
    eps_gbs = gbs_A_warm * (pow(stress, gbs_n-1) / pow(gs, p_grain_sz_exp)) *
      exp(-(gbs_Q_warm + pV)/RT);
  else
    eps_gbs = gbs_A_cold * (pow(stress, gbs_n-1) / pow(gs, p_grain_sz_exp)) *
      exp(-(gbs_Q_cold + pV)/RT);

  p.eps_diff=eps_diff;
  p.eps_disl=eps_disl;
  p.eps_basal=eps_basal;
  p.eps_gbs=eps_gbs;
  p.eps_total=eps_diff + eps_disl + (eps_basal * eps_gbs) / (eps_basal + eps_gbs);
  return p;
}
/*****************/

PetscErrorCode HybridIce::printInfo(PetscInt thresh) const {
  PetscErrorCode ierr;
  if (thresh <= getVerbosityLevel()) {
    ierr = view(NULL);CHKERRQ(ierr);
  }
  return 0;
}

PetscErrorCode HybridIce::view(PetscViewer viewer) const {
  PetscErrorCode ierr;
  PetscTruth iascii;

  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(comm,&viewer);CHKERRQ(ierr);
  }
  ierr = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&iascii);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"HybridIce object (%s)\n",prefix);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  No customizable options specific to HybridIce\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  Derived from ThermoGlenIce with the following state\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
    ierr = ThermoGlenIce::view(viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
  } else {
    SETERRQ(1,"No binary viewer for this object\n");
  }
  return 0;
}


HybridIceStripped::HybridIceStripped(MPI_Comm c,const char pre[]) : HybridIce(c,pre) {
  d_grain_size_stripped = 3.0e-3;  // m; = 3mm  (see Peltier et al 2000 paper)
}


PetscScalar HybridIceStripped::flow(PetscScalar stress, PetscScalar temp, PetscScalar pressure, PetscScalar) const {
  // note value of gs is ignored
  // note pressure only effects the temperature; the "P V" term is dropped
  // note no diffusional flow
  PetscScalar eps_disl, eps_basal, eps_gbs;

  if (PetscAbs(stress) < 1e-10) return 0;
  const PetscScalar T = temp + (beta_CC_grad / (rho * earth_grav)) * pressure;
  const PetscScalar RT = gasConst_R * T;
  // NO Diffusional Flow
  // Dislocation Creep
  if (T > disl_crit_temp)
    eps_disl = disl_A_warm * pow(stress, disl_n-1) * exp(-disl_Q_warm/RT);
  else
    eps_disl = disl_A_cold * pow(stress, disl_n-1) * exp(-disl_Q_cold/RT);
  // Basal Slip
  eps_basal = basal_A * pow(stress, basal_n-1) * exp(-basal_Q/RT);
  // Grain Boundary Sliding
  if (T > gbs_crit_temp)
    eps_gbs = gbs_A_warm *
              (pow(stress, gbs_n-1) / pow(d_grain_size_stripped, p_grain_sz_exp)) *
              exp(-gbs_Q_warm/RT);
  else
    eps_gbs = gbs_A_cold *
              (pow(stress, gbs_n-1) / pow(d_grain_size_stripped, p_grain_sz_exp)) *
              exp(-gbs_Q_cold/RT);

  return eps_disl + (eps_basal * eps_gbs) / (eps_basal + eps_gbs);
}


BedrockThermalType::BedrockThermalType() {
  rho    = 3300;  // kg/(m^3)     density
  k      = 3.0;   // J/(m K s) = W/(m K)    thermal conductivity
  c_p    = 1000;  // J/(kg K)     specific heat capacity
}


DeformableEarthType::DeformableEarthType() {
  // for following, reference Lingle & Clark (1985) and  Bueler, Lingle, & Kallen-Brown (2006)
  //    D = E T^3/(12 (1-nu^2)) for Young's modulus E = 6.6e10 N/m^2, lithosphere thickness
  //    T = 88 km, and Poisson's ratio nu = 0.5
  rho   = 3300;    // kg/(m^3)     density
  D     = 5.0e24;  // N m          lithosphere flexural rigidity
  eta   = 1.0e21;  // Pa s         half-space (mantle) viscosity
}


SeaWaterType::SeaWaterType() {
  rho            = 1028.0;  // kg/m^3         density
  // re Clausius-Clapeyron gradients:  Paterson (3rd ed, 1994, p. 212) says 
  //   T = T_0 - beta' P  where  beta' = 9.8e-5 K / kPa = 9.8e-8 K / Pa.
  //   And   dT/dz = beta' rho g  because  dP = - rho g dz.
  //   Thus:
  //     SeaWaterType:   beta = 9.8e-8 * 1028.0 * 9.81 = 9.882986e-4
  //     FreshWaterType: beta = 9.8e-8 * 1000.0 * 9.81 = 9.613800e-4
  //   For IceType this would be 8.748558e-4, but we use EISMINT II
  //   (Payne et al 2000) value of 8.66e-4 by default; see above.
  beta_CC_grad   = 9.883e-4;// K/m; C-C gradient
}


FreshWaterType::FreshWaterType() {
  rho          = 1000.0;  // kg/m^3         density
  beta_CC_grad = 9.614e-4;// K/m; C-C gradient; see comment above for SeaWaterType
}


PetscScalar BasalTypeSIA::velocity(PetscScalar sliding_coefficient,
                                   PetscScalar stress) {
  return sliding_coefficient * stress;
}



PlasticBasalType::PlasticBasalType(
             PetscScalar regularizationConstant, PetscTruth pseudoPlastic,
             PetscScalar pseudoExponent, PetscScalar pseudoUThreshold) {
  plastic_regularize = regularizationConstant;
  pseudo_plastic = pseudoPlastic;
  pseudo_q = pseudoExponent;
  pseudo_u_threshold = pseudoUThreshold;
}


PetscErrorCode PlasticBasalType::printInfo(int verbthresh, MPI_Comm com) {
  PetscErrorCode ierr;
  if (pseudo_plastic == PETSC_TRUE) {
    if (pseudo_q == 1.0) {
      ierr = verbPrintf(verbthresh, com, 
        "Using linearly viscous till with u_threshold = %.2f m/a.\n", 
        pseudo_u_threshold * secpera); CHKERRQ(ierr);
    } else {
      ierr = verbPrintf(verbthresh, com, 
        "Using pseudo-plastic till with eps = %10.5e m/a, q = %.4f,"
        " and u_threshold = %.2f m/a.\n", 
        plastic_regularize * secpera, pseudo_q, pseudo_u_threshold * secpera); 
        CHKERRQ(ierr);
    }
  } else {
    ierr = verbPrintf(verbthresh, com, 
      "Using purely plastic till with eps = %10.5e m/a.\n",
      plastic_regularize * secpera); CHKERRQ(ierr);
  }
  return 0;
}


//! Compute the drag coefficient for the basal shear stress.
/*!
The basal shear stress term \f$\tau_b\f$ in the SSA stress balance for ice
is minus the return value here times (vx,vy).

Purely plastic is the pseudo_q = 0.0 case; linear is pseudo_q = 1.0; set 
pseudo_q using PlasticBasalType constructor.
 */
PetscScalar PlasticBasalType::drag(PetscScalar tauc,
                                   PetscScalar vx, PetscScalar vy) {
  const PetscScalar magreg2 = PetscSqr(plastic_regularize) + PetscSqr(vx) + PetscSqr(vy);
  if (pseudo_plastic == PETSC_TRUE) {
    return tauc * pow(magreg2, 0.5*(pseudo_q - 1)) * pow(pseudo_u_threshold, -pseudo_q);
  } else { // pure plastic, but regularized
    return tauc / sqrt(magreg2);
  }
}

// Derivative of drag with respect to \f$ \alpha = \frac 1 2 (u_x^2 + u_y^2) \f$
void PlasticBasalType::dragWithDerivative(PetscReal tauc, PetscScalar vx, PetscScalar vy, PetscScalar *d, PetscScalar *dd) const
{
  const PetscScalar magreg2 = PetscSqr(plastic_regularize) + PetscSqr(vx) + PetscSqr(vy);
  if (pseudo_plastic == PETSC_TRUE) {
    *d = tauc * pow(magreg2, 0.5*(pseudo_q - 1)) * pow(pseudo_u_threshold, -pseudo_q);
    if (dd) *dd = (pseudo_q - 1) * *d / magreg2;
  } else { // pure plastic, but regularized
    *d = tauc / sqrt(magreg2);
    if (dd) *dd = -1 * *d / magreg2;
  }
}


SSAStrengthExtension::SSAStrengthExtension() {
  min_thickness = 50.0;   // m
          // minimum thickness (for SSA velocity computation) at which 
          // NuH switches from vertical integral to constant value
          // this value strongly related to calving front
          // force balance, but the geometry itself is not affected by this value
  const PetscReal
    DEFAULT_CONSTANT_HARDNESS_FOR_SSA = 1.9e8,  // Pa s^{1/3}; see p. 49 of MacAyeal et al 1996
    DEFAULT_TYPICAL_STRAIN_RATE = (100.0 / secpera) / (100.0 * 1.0e3);  // typical strain rate is 100 m/yr per 
  nuH = min_thickness * DEFAULT_CONSTANT_HARDNESS_FOR_SSA
                       / (2.0 * pow(DEFAULT_TYPICAL_STRAIN_RATE,2./3.)); // Pa s m
          // COMPARE: 30.0 * 1e6 * secpera = 9.45e14 is Ritz et al (2001) value of
          //          30 MPa yr for \bar\nu
}

SSAStrengthExtension::~SSAStrengthExtension() {
  // do nothing
}

PetscErrorCode SSAStrengthExtension::set_notional_strength(PetscReal my_nuH) {
  nuH = my_nuH;
  return 0;
}

PetscErrorCode SSAStrengthExtension::set_min_thickness(PetscReal my_min_thickness) {
  min_thickness = my_min_thickness;
  return 0;
}

PetscReal SSAStrengthExtension::notional_strength() const {
  return nuH;
}

PetscReal SSAStrengthExtension::min_thickness_for_extension() const {
  return min_thickness;
}


#undef ALEN
#define ALEN(a) (sizeof(a)/sizeof(a)[0])

IceFactory::IceFactory(MPI_Comm c,const char pre[])
{
  comm = c;
  prefix[0] = 0;
  if (pre) {
    PetscStrncpy(prefix,pre,sizeof(prefix));
  }
  if (registerAll()) {
    PetscPrintf(comm,"IceFactory::registerAll returned an error but we're in a constructor\n");
    PetscEnd();
  }
  if (setType(ICE_PB)) {       // Set's a default type
    PetscPrintf(comm,"IceFactory::setType(\"%s\") returned an error, but we're in a constructor\n",ICE_PB);
    PetscEnd();
  }
}

IceFactory::~IceFactory()
{
  PetscFListDestroy(&type_list);
}

#undef __FUNCT__
#define __FUNCT__ "IceFactory::registerType"
PetscErrorCode IceFactory::registerType(const char tname[],PetscErrorCode(*icreate)(MPI_Comm,const char[],IceType**))
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscFListAdd(&type_list,tname,NULL,(void(*)(void))icreate);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


static PetscErrorCode create_custom(MPI_Comm comm,const char pre[],IceType **i) {
  *i = new (CustomGlenIce)(comm,pre);  return 0;
}
static PetscErrorCode create_pb(MPI_Comm comm,const char pre[],IceType **i) {
  *i = new (ThermoGlenIce)(comm,pre);  return 0;
}
static PetscErrorCode create_hooke(MPI_Comm comm,const char pre[],IceType **i) {
  *i = new (ThermoGlenIceHooke)(comm,pre);  return 0;
}
static PetscErrorCode create_arr(MPI_Comm comm,const char pre[],IceType **i) {
  *i = new (ThermoGlenArrIce)(comm,pre);  return 0;
}
static PetscErrorCode create_arrwarm(MPI_Comm comm,const char pre[],IceType **i) {
  *i = new (ThermoGlenArrIceWarm)(comm,pre);  return 0;
}
static PetscErrorCode create_hybrid(MPI_Comm comm,const char pre[],IceType **i) {
  *i = new (HybridIce)(comm,pre);  return 0;
}


#undef __FUNCT__
#define __FUNCT__ "IceFactory::registerAll"
PetscErrorCode IceFactory::registerAll()
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscMemzero(&type_list,sizeof(type_list));CHKERRQ(ierr);
  ierr = registerType(ICE_CUSTOM, &create_custom); CHKERRQ(ierr);
  ierr = registerType(ICE_PB,     &create_pb);     CHKERRQ(ierr);
  ierr = registerType(ICE_HOOKE,  &create_hooke);  CHKERRQ(ierr);
  ierr = registerType(ICE_ARR,    &create_arr);    CHKERRQ(ierr);
  ierr = registerType(ICE_ARRWARM,&create_arrwarm);CHKERRQ(ierr);
  ierr = registerType(ICE_HYBRID, &create_hybrid); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "IceFactory::setType"
PetscErrorCode IceFactory::setType(const char type[])
{
  void (*r)(void);
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscFListFind(type_list,comm,type,(void(**)(void))&r);CHKERRQ(ierr);
  if (!r) SETERRQ1(PETSC_ERR_ARG_UNKNOWN_TYPE,"Selected Ice type %s not available",type);
  ierr = PetscStrncpy(type_name,type,sizeof(type_name));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "IceFactory::setTypeByNumber"
// This method exists only for backwards compatibility.
PetscErrorCode IceFactory::setTypeByNumber(int n)
{

  PetscFunctionBegin;
  switch (n) {
    case 0: setType(ICE_PB); break;
    case 1: setType(ICE_ARR); break;
    case 2: setType(ICE_ARRWARM); break;
    case 3: setType(ICE_HOOKE); break;
    case 4: setType(ICE_HYBRID); break;
    default: SETERRQ1(1,"Ice number %d not available",n);
  }
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "IceFactory::setFromOptions"
PetscErrorCode IceFactory::setFromOptions()
{
  PetscErrorCode ierr;
  PetscTruth flg;
  char my_type_name[256];

  PetscFunctionBegin;
  {
    // This is for backwards compatibility only, -ice_type is the new way to choose ice
    // Note that it is not maintainable to have multiple options for the same thing
    PetscInt n;
    ierr = PetscOptionsGetInt(prefix, "-law", &n, &flg); CHKERRQ(ierr);
    if (flg) {
      ierr = verbPrintf(1,comm,"Option `-law' is deprecated, please use `-ice_type'\n");CHKERRQ(ierr);
      ierr = setTypeByNumber(n);CHKERRQ(ierr);
    }
  }
  // These options will choose Goldsby-Kohlstedt ice by default (see IceModel::setFromOptions()) but if a derived class
  // uses a different initialization procedure, we'll recognize them here as well.  A better long-term solution would be
  // to separate tracking of grain size from a particular flow law (since in principle they are unrelated) but since
  // HYBRID is the only one that currently uses grain size, this solution is acceptable.
  ierr = PetscOptionsHasName(prefix, "-gk_age", &flg); CHKERRQ(ierr);
  if (flg) {
    ierr = setType(ICE_HYBRID);CHKERRQ(ierr);
  }
  // -gk 0 does not make sense, so using PetscOptionsHasName is OK.
  ierr = PetscOptionsHasName(prefix, "-gk", &flg); CHKERRQ(ierr);
  if (flg) {
    ierr = setType(ICE_HYBRID);CHKERRQ(ierr);
  }
  ierr = PetscOptionsBegin(comm,prefix,"IceFactory options","IceType");CHKERRQ(ierr);
  {
    ierr = PetscOptionsList("-ice_type","Ice type","IceFactory::setType",
                            type_list,type_name,my_type_name,sizeof(my_type_name),&flg);CHKERRQ(ierr);
    if (flg) {ierr = setType(my_type_name);CHKERRQ(ierr);}
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  
//  ierr = PetscPrintf(comm,"IceFactory::type_name=%s at end of IceFactory::setFromOptions()\n",
//                     type_name); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "IceFactory::create"
PetscErrorCode IceFactory::create(IceType **inice)
{
  PetscErrorCode ierr,(*r)(MPI_Comm,const char[],IceType**);
  IceType *ice;

  PetscFunctionBegin;
  PetscValidPointer(inice,3);
  *inice = 0;
  ierr = PetscFListFind(type_list,comm,type_name,(void(**)(void))&r);CHKERRQ(ierr);
  if (!r) SETERRQ1(1,"Selected Ice type %s not available, but we shouldn't be able to get here anyway",type_name);
  ierr = (*r)(comm,prefix,&ice);CHKERRQ(ierr);
  *inice = ice;
  PetscFunctionReturn(0);
}

