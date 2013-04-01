// Copyright (C) 2011, 2012, 2013 PISM Authors
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

#include "PAAnomaly.hh"
#include "IceGrid.hh"
#include <assert.h>

PAAnomaly::PAAnomaly(IceGrid &g, const NCConfigVariable &conf, PISMAtmosphereModel* in)
  : PGivenClimate<PAModifier,PISMAtmosphereModel>(g, conf, in) {
  PetscErrorCode ierr = allocate_PAAnomaly(); CHKERRCONTINUE(ierr);
  if (ierr != 0)
    PISMEnd();

}

PetscErrorCode PAAnomaly::allocate_PAAnomaly() {
  PetscErrorCode ierr;

  temp_name      = "air_temp_anomaly";
  mass_flux_name = "precipitation_anomaly";
  option_prefix  = "-atmosphere_anomaly";

  ierr = process_options(); CHKERRQ(ierr);

  ierr = set_vec_parameters("", ""); CHKERRQ(ierr);

  ierr = temp.create(grid, temp_name, false); CHKERRQ(ierr);
  ierr = mass_flux.create(grid, mass_flux_name, false); CHKERRQ(ierr);

  ierr = temp.set_attrs("climate_forcing", "anomaly of the near-surface air temperature",
                        "Kelvin", ""); CHKERRQ(ierr);
  ierr = mass_flux.set_attrs("climate_forcing", "anomaly of the ice-equivalent precipitation rate",
                             "m s-1", ""); CHKERRQ(ierr);
  ierr = mass_flux.set_glaciological_units("m year-1"); CHKERRQ(ierr);
  mass_flux.write_in_glaciological_units = true;

  air_temp.init_2d("air_temp", grid);
  air_temp.set_string("pism_intent", "diagnostic");
  air_temp.set_string("long_name", "near-surface air temperature");
  ierr = air_temp.set_units("K"); CHKERRQ(ierr);

  precipitation.init_2d("precipitation", grid);
  precipitation.set_string("pism_intent", "diagnostic");
  precipitation.set_string("long_name", "near-surface air temperature");
  ierr = precipitation.set_units("m / s"); CHKERRQ(ierr);
  ierr = precipitation.set_glaciological_units("m / year"); CHKERRQ(ierr);

  return 0;
}

PAAnomaly::~PAAnomaly()
{
  // empty
}

PetscErrorCode PAAnomaly::init(PISMVars &vars) {
  PetscErrorCode ierr;

  t = dt = GSL_NAN;  // every re-init restarts the clock

  assert(input_model != NULL);
  ierr = input_model->init(vars); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com,
                    "* Initializing the -atmosphere ...,anomaly code...\n"); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com,
                    "    reading anomalies from %s ...\n",
                    filename.c_str()); CHKERRQ(ierr);

  ierr = temp.init(filename, bc_period, bc_reference_time); CHKERRQ(ierr);
  ierr = mass_flux.init(filename, bc_period, bc_reference_time); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PAAnomaly::update(PetscReal my_t, PetscReal my_dt) {
  PetscErrorCode ierr = update_internal(my_t, my_dt); CHKERRQ(ierr);

  ierr = mass_flux.average(t, dt); CHKERRQ(ierr);
  ierr = temp.average(t, dt); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode PAAnomaly::mean_precipitation(IceModelVec2S &result) {
  PetscErrorCode ierr = input_model->mean_precipitation(result); CHKERRQ(ierr);

  return result.add(1.0, mass_flux);
}

PetscErrorCode PAAnomaly::mean_annual_temp(IceModelVec2S &result) {
  PetscErrorCode ierr = input_model->mean_annual_temp(result); CHKERRQ(ierr);

  return result.add(1.0, temp);
}

PetscErrorCode PAAnomaly::temp_snapshot(IceModelVec2S &result) {
  PetscErrorCode ierr = input_model->temp_snapshot(result); CHKERRQ(ierr);

  return result.add(1.0, temp);
}


PetscErrorCode PAAnomaly::begin_pointwise_access() {
  PetscErrorCode ierr = input_model->begin_pointwise_access(); CHKERRQ(ierr);
  ierr = temp.begin_access(); CHKERRQ(ierr);
  ierr = mass_flux.begin_access(); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PAAnomaly::end_pointwise_access() {
  PetscErrorCode ierr = input_model->end_pointwise_access(); CHKERRQ(ierr);
  ierr = temp.end_access(); CHKERRQ(ierr);
  ierr = mass_flux.end_access(); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PAAnomaly::init_timeseries(PetscReal *ts, unsigned int N) {
  PetscErrorCode ierr;
  ierr = input_model->init_timeseries(ts, N); CHKERRQ(ierr);

  ierr = temp.init_interpolation(ts, N); CHKERRQ(ierr);

  ierr = mass_flux.init_interpolation(ts, N); CHKERRQ(ierr);

  m_ts_times.resize(N);
  for (unsigned int k = 0; k < m_ts_times.size(); ++k)
    m_ts_times[k] = ts[k];
  
  return 0;
}

PetscErrorCode PAAnomaly::temp_time_series(int i, int j, PetscReal *result) {
  PetscErrorCode ierr;
  vector<double> temp_anomaly(m_ts_times.size());
  ierr = input_model->temp_time_series(i, j, result); CHKERRQ(ierr);

  ierr = temp.interp(i, j, &temp_anomaly[0]); CHKERRQ(ierr);

  for (unsigned int k = 0; k < m_ts_times.size(); ++k)
    result[k] += temp_anomaly[k];

  return 0;
}

PetscErrorCode PAAnomaly::precip_time_series(int i, int j, PetscReal *result) {
  PetscErrorCode ierr;

  ierr = input_model->precip_time_series(i, j, result); CHKERRQ(ierr);

  m_mass_flux_anomaly.reserve(m_ts_times.size());
  ierr = mass_flux.interp(i, j, &m_mass_flux_anomaly[0]); CHKERRQ(ierr);

  for (unsigned int k = 0; k < m_ts_times.size(); ++k)
    result[k] += m_mass_flux_anomaly[k];

  return 0;
}

void PAAnomaly::add_vars_to_output(string keyword,
                                   map<string,NCSpatialVariable> &result) {
  input_model->add_vars_to_output(keyword, result);

  if (keyword == "medium" || keyword == "big") {
    result["air_temp"] = air_temp;
    result["precipitation"] = precipitation;
  }
}


PetscErrorCode PAAnomaly::define_variables(set<string> vars, const PIO &nc,
                                           PISM_IO_Type nctype) {
  PetscErrorCode ierr;

  if (set_contains(vars, "air_temp")) {
    ierr = air_temp.define(nc, nctype, false); CHKERRQ(ierr);
    vars.erase("air_temp");
  }

  if (set_contains(vars, "precipitation")) {
    ierr = precipitation.define(nc, nctype, true); CHKERRQ(ierr);
    vars.erase("precipitation");
  }

  ierr = input_model->define_variables(vars, nc, nctype); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode PAAnomaly::write_variables(set<string> vars, string file) {
  PetscErrorCode ierr;

  if (set_contains(vars, "air_temp")) {
    IceModelVec2S tmp;
    ierr = tmp.create(grid, "air_temp", false); CHKERRQ(ierr);
    ierr = tmp.set_metadata(air_temp, 0); CHKERRQ(ierr);

    ierr = mean_annual_temp(tmp); CHKERRQ(ierr);

    ierr = tmp.write(file); CHKERRQ(ierr);

    vars.erase("air_temp");
  }

  if (set_contains(vars, "precipitation")) {
    IceModelVec2S tmp;
    ierr = tmp.create(grid, "precipitation", false); CHKERRQ(ierr);
    ierr = tmp.set_metadata(precipitation, 0); CHKERRQ(ierr);

    ierr = mean_precipitation(tmp); CHKERRQ(ierr);

    tmp.write_in_glaciological_units = true;
    ierr = tmp.write(file); CHKERRQ(ierr);

    vars.erase("precipitation");
  }

  ierr = input_model->write_variables(vars, file); CHKERRQ(ierr);

  return 0;
}

