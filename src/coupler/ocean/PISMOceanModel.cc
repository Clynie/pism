/* Copyright (C) 2013, 2014, 2015 PISM Authors
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

#include "coupler/PISMOcean.hh"
#include "base/util/iceModelVec.hh"

namespace pism {
namespace ocean {
OceanModel::OceanModel(IceGrid::ConstPtr g)
  : Component_TS(g), m_sea_level(0) {
  // empty
}

OceanModel::~OceanModel() {
  // empty
}

void OceanModel::init() {
  this->init_impl();
}

double OceanModel::sea_level_elevation() {
  double result;
  this->sea_level_elevation_impl(result);
  return result;
}

void OceanModel::shelf_base_temperature(IceModelVec2S &result) {
  this->shelf_base_temperature_impl(result);
}

void OceanModel::shelf_base_mass_flux(IceModelVec2S &result) {
  this->shelf_base_mass_flux_impl(result);
}

void OceanModel::melange_back_pressure_fraction(IceModelVec2S &result) {
  this->melange_back_pressure_fraction_impl(result);
}

/** Set `result` to the melange back pressure fraction.
 *
 * This default implementation sets `result` to 0.0.
 *
 * @param[out] result back pressure fraction
 *
 * @return 0 on success
 */
void OceanModel::melange_back_pressure_fraction_impl(IceModelVec2S &result) {
  result.set(0.0);
}

} // end of namespace ocean
} // end of namespace pism
