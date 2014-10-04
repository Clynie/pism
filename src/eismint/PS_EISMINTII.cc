/* Copyright (C) 2014 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "PS_EISMINTII.hh"
#include "PISMAtmosphere.hh"
#include "PISMConfig.hh"
#include "pism_options.hh"

namespace pism {

PS_EISMINTII::PS_EISMINTII(IceGrid &g, const Config &conf, int experiment)
  : SurfaceModel(g, conf), m_experiment(experiment) {
  PetscErrorCode ierr = allocate();
  if (ierr != 0) {
    PetscPrintf(grid.com, "PISM ERROR: memory allocation failed");
    PISMEnd();
  }
}

PetscErrorCode PS_EISMINTII::allocate() {
  PetscErrorCode ierr;

  ierr = m_climatic_mass_balance.create(grid, "climatic_mass_balance", WITHOUT_GHOSTS); CHKERRQ(ierr);
  ierr = m_climatic_mass_balance.set_attrs("internal",
                                           "ice-equivalent surface mass balance (accumulation/ablation) rate",
                                           "kg m-2 s-1",
                                           "land_ice_surface_specific_mass_balance_flux"); CHKERRQ(ierr);
  ierr = m_climatic_mass_balance.set_glaciological_units("kg m-2 year-1"); CHKERRQ(ierr);
  m_climatic_mass_balance.write_in_glaciological_units = true;
  m_climatic_mass_balance.metadata().set_string("comment", "positive values correspond to ice gain");

  // annual mean air temperature at "ice surface", at level below all
  // firn processes (e.g. "10 m" or ice temperatures)
  ierr = m_ice_surface_temp.create(grid, "ice_surface_temp", WITHOUT_GHOSTS); CHKERRQ(ierr);
  ierr = m_ice_surface_temp.set_attrs("internal",
                                      "annual average ice surface temperature, below firn processes",
                                      "K", ""); CHKERRQ(ierr);
  return 0;
}

PS_EISMINTII::~PS_EISMINTII() {
  // empty
}

PetscErrorCode PS_EISMINTII::init(Vars &vars) {
  PetscErrorCode ierr;

  (void) vars;

  ierr = PetscOptionsBegin(grid.com, "", "EISMINT II climate input options", ""); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com, 
                    "setting parameters for surface mass balance"
                    " and temperature in EISMINT II experiment %c ... \n", 
                    m_experiment); CHKERRQ(ierr);

  // EISMINT II specified values for parameters
  m_S_b = grid.convert(1.0e-2 * 1e-3, "1/year", "1/s"); // Grad of accum rate change
  m_S_T = 1.67e-2 * 1e-3;       // K/m  Temp gradient

  // these are for A,E,G,H,I,K:
  m_M_max = grid.convert(0.5, "m/year", "m/s"); // Max accumulation
  m_R_el  = 450.0e3;            // Distance to equil line (SMB=0)
  m_T_min = 238.15;

  switch (m_experiment) {
  case 'B':                     // supposed to start from end of experiment A and:
    m_T_min = 243.15;
    break;
  case 'C':
  case 'J':
  case 'L':                     // supposed to start from end of experiment A (for C;
    //   resp I and K for J and L) and:
    m_M_max = grid.convert(0.25, "m/year", "m/s");
    m_R_el  = 425.0e3;
    break;
  case 'D':                     // supposed to start from end of experiment A and:
    m_R_el  = 425.0e3;
    break;
  case 'F':                     // start with zero ice and:
    m_T_min = 223.15;
    break;
  }

  // if user specifies Tmin, Tmax, Mmax, Sb, ST, Rel, then use that (override above)
  bool option_set = false;
  ierr = OptionsReal("-Tmin", "T min, Kelvin", m_T_min, option_set); CHKERRQ(ierr);

  double
    myMmax = grid.convert(m_M_max, "m/s",        "m/year"),
    mySb   = grid.convert(m_S_b,   "m/second/m", "m/year/km"),
    myST   = grid.convert(m_S_T,   "K/m",        "K/km"),
    myRel  = grid.convert(m_R_el,  "m",          "km");

  ierr = OptionsReal("-Mmax", "Maximum accumulation, m/year",
                         myMmax, option_set); CHKERRQ(ierr);
  if (option_set)
    m_M_max = grid.convert(myMmax, "m/year", "m/second");

  ierr = OptionsReal("-Sb", "radial gradient of accumulation rate, (m/year)/km",
                         mySb, option_set); CHKERRQ(ierr);
  if (option_set)
    m_S_b = grid.convert(mySb, "m/year/km", "m/second/m");

  ierr = OptionsReal("-ST", "radial gradient of surface temperature, K/km",
                         myST, option_set); CHKERRQ(ierr);
  if (option_set)
    m_S_T = grid.convert(myST, "K/km", "K/m");

  ierr = OptionsReal("-Rel", "radial distance to equilibrium line, km",
                         myRel, option_set); CHKERRQ(ierr);
  if (option_set)
    m_R_el = grid.convert(myRel, "km", "m");

  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  ierr = initialize_using_formulas(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PS_EISMINTII::initialize_using_formulas() {
  PetscErrorCode ierr;

  PetscScalar cx = grid.Lx, cy = grid.Ly;
  if (m_experiment == 'E') {
    // shift center
    cx += 100.0e3;
    cy += 100.0e3;
  }

  // now fill in accum and surface temp
  IceModelVec::AccessList list;
  list.add(m_ice_surface_temp);
  list.add(m_climatic_mass_balance);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    // r is distance from center of grid; if E then center is shifted (above)
    const double r = sqrt(PetscSqr(-cx + grid.dx*i)
                          + PetscSqr(-cy + grid.dy*j));
    // set accumulation from formula (7) in (Payne et al 2000)
    m_climatic_mass_balance(i,j) = PetscMin(m_M_max, m_S_b * (m_R_el-r));
    // set surface temperature
    m_ice_surface_temp(i,j) = m_T_min + m_S_T * r;  // formula (8) in (Payne et al 2000)
  }

  // convert from [m/s] to [kg m-2 s-1]
  ierr = m_climatic_mass_balance.scale(config.get("ice_density")); CHKERRQ(ierr);

  return 0;
}

void PS_EISMINTII::attach_atmosphere_model(AtmosphereModel *input) {
  delete input;
}

PetscErrorCode PS_EISMINTII::ice_surface_mass_flux(IceModelVec2S &result) {
  PetscErrorCode ierr = m_climatic_mass_balance.copy_to(result); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PS_EISMINTII::ice_surface_temperature(IceModelVec2S &result) {
  PetscErrorCode ierr = m_ice_surface_temp.copy_to(result); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PS_EISMINTII::update(PetscReal t, PetscReal dt) {
  (void) t;
  (void) dt;

  // do nothing (but an implementation is required)

  return 0;
}

void PS_EISMINTII::add_vars_to_output(const std::string &keyword, std::set<std::string> &result) {
  (void) keyword;

  result.insert(m_climatic_mass_balance.metadata().get_name());
  result.insert(m_ice_surface_temp.metadata().get_name());
}

PetscErrorCode PS_EISMINTII::define_variables(const std::set<std::string> &vars, const PIO &nc,
                                             IO_Type nctype) {
  PetscErrorCode ierr;

  if (set_contains(vars, m_climatic_mass_balance.metadata().get_name())) {
    ierr = m_climatic_mass_balance.define(nc, nctype); CHKERRQ(ierr);
  }

  if (set_contains(vars, m_ice_surface_temp.metadata().get_name())) {
    ierr = m_ice_surface_temp.define(nc, nctype); CHKERRQ(ierr);
  }

  return 0;
}

PetscErrorCode PS_EISMINTII::write_variables(const std::set<std::string> &vars, const PIO &nc) {
  PetscErrorCode ierr;

  if (set_contains(vars, m_climatic_mass_balance.metadata().get_name())) {
    ierr = m_climatic_mass_balance.write(nc); CHKERRQ(ierr);
  }

  if (set_contains(vars, m_ice_surface_temp.metadata().get_name())) {
    ierr = m_ice_surface_temp.write(nc); CHKERRQ(ierr);
  }

  return 0;
}

} // end of namespace pism
