/* Copyright (C) 2014, 2015 PISM Authors
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

#include "PSFormulas.hh"
#include "coupler/PISMAtmosphere.hh"
#include "base/util/pism_const.hh"

namespace pism {
namespace surface {

PSFormulas::PSFormulas(IceGrid::ConstPtr g)
  : SurfaceModel(g) {
  m_climatic_mass_balance.create(m_grid, "climatic_mass_balance", WITHOUT_GHOSTS);
  m_climatic_mass_balance.set_attrs("internal",
                                    "ice-equivalent surface mass balance (accumulation/ablation) rate",
                                    "kg m-2 s-1",
                                    "land_ice_surface_specific_mass_balance_flux");
  m_climatic_mass_balance.metadata().set_string("glaciological_units", "kg m-2 year-1");
  m_climatic_mass_balance.write_in_glaciological_units = true;
  m_climatic_mass_balance.metadata().set_string("comment", "positive values correspond to ice gain");

  // annual mean air temperature at "ice surface", at level below all
  // firn processes (e.g. "10 m" or ice temperatures)
  m_ice_surface_temp.create(m_grid, "ice_surface_temp", WITHOUT_GHOSTS);
  m_ice_surface_temp.set_attrs("internal",
                               "annual average ice surface temperature, below firn processes",
                               "K", "");
}

PSFormulas::~PSFormulas() {
  // empty
}


void PSFormulas::attach_atmosphere_model_impl(atmosphere::AtmosphereModel *input) {
  delete input;
}

void PSFormulas::ice_surface_mass_flux_impl(IceModelVec2S &result) {
  result.copy_from(m_climatic_mass_balance);
}

void PSFormulas::ice_surface_temperature_impl(IceModelVec2S &result) {
  result.copy_from(m_ice_surface_temp);
}

void PSFormulas::add_vars_to_output_impl(const std::string &keyword, std::set<std::string> &result) {
  (void) keyword;

  result.insert(m_climatic_mass_balance.metadata().get_name());
  result.insert(m_ice_surface_temp.metadata().get_name());
}

void PSFormulas::define_variables_impl(const std::set<std::string> &vars, const PIO &nc,
                                             IO_Type nctype) {

  if (set_contains(vars, m_climatic_mass_balance.metadata().get_name())) {
    m_climatic_mass_balance.define(nc, nctype);
  }

  if (set_contains(vars, m_ice_surface_temp.metadata().get_name())) {
    m_ice_surface_temp.define(nc, nctype);
  }
}

void PSFormulas::write_variables_impl(const std::set<std::string> &vars, const PIO &nc) {

  if (set_contains(vars, m_climatic_mass_balance.metadata().get_name())) {
    m_climatic_mass_balance.write(nc);
  }

  if (set_contains(vars, m_ice_surface_temp.metadata().get_name())) {
    m_ice_surface_temp.write(nc);
  }
}

} // end of namespace surface
} // end of namespace pism
