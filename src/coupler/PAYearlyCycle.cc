// Copyright (C) 2008-2011 Ed Bueler, Constantine Khroulev, Ricarda Winkelmann,
// Gudfinna Adalgeirsdottir and Andy Aschwanden
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

// Implementation of the atmosphere model using constant-in-time precipitation
// and a cosine yearly cycle for near-surface air temperatures.

// This includes the SeaRISE Greenland parameterization.

#include "PISMAtmosphere.hh"

//! Allocates memory and reads in the precipitaion data.
PetscErrorCode PAYearlyCycle::init(PISMVars &vars) {
  PetscErrorCode ierr;
  bool regrid = false;
  int start = -1;

  variables = &vars;

  snow_temp_july_day = config.get("snow_temp_july_day");

  // Allocate internal IceModelVecs:
  ierr = temp_ma.create(grid, "airtemp_ma", false); CHKERRQ(ierr);
  ierr = temp_ma.set_attrs("diagnostic",
			   "mean annual near-surface air temperature (without sub-year time-dependence or forcing)",
			   "K", 
			   ""); CHKERRQ(ierr);  // no CF standard_name ??
  ierr = temp_ma.set_attr("source", reference);

  ierr = temp_mj.create(grid, "airtemp_mj", false); CHKERRQ(ierr);
  ierr = temp_mj.set_attrs("diagnostic",
			   "mean July near-surface air temperature (without sub-year time-dependence or forcing)",
			   "Kelvin",
			   ""); CHKERRQ(ierr);  // no CF standard_name ??
  ierr = temp_mj.set_attr("source", reference);

  ierr = precip.create(grid, "precip", false); CHKERRQ(ierr);
  ierr = precip.set_attrs("climate_state", 
			      "mean annual ice-equivalent precipitation rate",
			      "m s-1", 
			      ""); CHKERRQ(ierr); // no CF standard_name ??
  ierr = precip.set_glaciological_units("m year-1");
  precip.write_in_glaciological_units = true;
  precip.time_independent = true;

  ierr = find_pism_input(precip_filename, regrid, start); CHKERRQ(ierr);

  // read precipitation rate from file
  ierr = verbPrintf(2, grid.com, 
		    "    reading mean annual ice-equivalent precipitation rate 'precip'\n"
		    "      from %s ... \n",
		    precip_filename.c_str()); CHKERRQ(ierr); 
  if (regrid) {
    ierr = precip.regrid(precip_filename.c_str(), true); CHKERRQ(ierr); // fails if not found!
  } else {
    ierr = precip.read(precip_filename.c_str(), start); CHKERRQ(ierr); // fails if not found!
  }
  string precip_history = "read from " + precip_filename + "\n";

  ierr = precip.set_attr("history", precip_history); CHKERRQ(ierr);

  airtemp_var.init_2d("airtemp", grid);
  airtemp_var.set_string("pism_intent", "diagnostic");
  airtemp_var.set_string("long_name",
                         "snapshot of the near-surface air temperature");
  ierr = airtemp_var.set_units("K"); CHKERRQ(ierr);

  return 0;
}

void PAYearlyCycle::add_vars_to_output(string keyword, set<string> &result) {
  result.insert("precip");

  if (keyword == "big") {
    result.insert("airtemp_ma");
    result.insert("airtemp_mj");
    result.insert("airtemp");
  }
}

PetscErrorCode PAYearlyCycle::define_variables(set<string> vars, const NCTool &nc, nc_type nctype) {
  PetscErrorCode ierr;
  int varid;

  if (set_contains(vars, "airtemp")) {
    ierr = airtemp_var.define(nc, varid, nctype, false); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_ma")) {
    ierr = temp_ma.define(nc, nctype); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_mj")) {
    ierr = temp_mj.define(nc, nctype); CHKERRQ(ierr);
  }

  if (set_contains(vars, "precip")) {
    ierr = precip.define(nc, nctype); CHKERRQ(ierr);
  }

  return 0;
}


PetscErrorCode PAYearlyCycle::write_variables(set<string> vars, string filename) {
  PetscErrorCode ierr;

  if (set_contains(vars, "airtemp")) {
    IceModelVec2S airtemp;
    ierr = airtemp.create(grid, "airtemp", false); CHKERRQ(ierr);
    ierr = airtemp.set_metadata(airtemp_var, 0); CHKERRQ(ierr);

    ierr = temp_snapshot(airtemp); CHKERRQ(ierr);

    ierr = airtemp.write(filename.c_str()); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_ma")) {
    ierr = temp_ma.write(filename.c_str()); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_mj")) {
    ierr = temp_mj.write(filename.c_str()); CHKERRQ(ierr);
  }

  if (set_contains(vars, "precip")) {
    ierr = precip.write(filename.c_str()); CHKERRQ(ierr);
  }

  return 0;
}

//! Copies the stored precipitation field into result.
PetscErrorCode PAYearlyCycle::mean_precip(IceModelVec2S &result) {
  PetscErrorCode ierr;

  string precip_history = "read from " + precip_filename + "\n";

  ierr = precip.copy_to(result); CHKERRQ(ierr);
  ierr = result.set_attr("history", precip_history); CHKERRQ(ierr);

  return 0;
}

//! Copies the stored mean annual near-surface air temperature field into result.
PetscErrorCode PAYearlyCycle::mean_annual_temp(IceModelVec2S &result) {
  PetscErrorCode ierr;

  ierr = temp_ma.copy_to(result); CHKERRQ(ierr);
  ierr = result.set_attr("history",
			 "computed using " + reference + "\n"); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PAYearlyCycle::temp_time_series(int i, int j, int N,
							      PetscReal *ts, PetscReal *values) {
  // constants related to the standard yearly cycle
  const PetscReal
    radpersec = 2.0 * pi / secpera, // radians per second frequency for annual cycle
    sperd = 8.64e4, // exact number of seconds per day
    julydaysec = sperd * snow_temp_july_day;

  for (PetscInt k = 0; k < N; ++k) {
    double tk = ( ts[k] - floor(ts[k]) ) * secpera; // time from the beginning of a year, in seconds
    values[k] = temp_ma(i,j) + (temp_mj(i,j) - temp_ma(i,j)) * cos(radpersec * (tk - julydaysec));
  }

  return 0;
}

PetscErrorCode PAYearlyCycle::temp_snapshot(IceModelVec2S &result) {
  PetscErrorCode ierr;
  const PetscReal
    radpersec = 2.0 * pi / secpera, // radians per second frequency for annual cycle
    sperd = 8.64e4, // exact number of seconds per day
    julydaysec = sperd * config.get("snow_temp_july_day");

  double T = t + 0.5 * dt;

  double t_sec = ( T - floor(T) ) * secpera; // time from the beginning of a year, in seconds

  ierr = temp_mj.add(-1.0, temp_ma, result); CHKERRQ(ierr); // tmp = temp_mj - temp_ma
  ierr = result.scale(cos(radpersec * (t_sec - julydaysec))); CHKERRQ(ierr);
  ierr = result.add(1.0, temp_ma); CHKERRQ(ierr);
  // result = temp_ma + (temp_mj - temp_ma) * cos(radpersec * (T - julydaysec));

  string history = "computed using " + reference + "\n";
  ierr = result.set_attr("history", history); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PAYearlyCycle::begin_pointwise_access() {
  PetscErrorCode ierr;

  ierr = temp_ma.begin_access(); CHKERRQ(ierr);
  ierr = temp_mj.begin_access(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PAYearlyCycle::end_pointwise_access() {
  PetscErrorCode ierr;

  ierr = temp_ma.end_access(); CHKERRQ(ierr);
  ierr = temp_mj.end_access(); CHKERRQ(ierr);

  return 0;
}

///// PA_SeaRISE_Greenland

PetscErrorCode PA_SeaRISE_Greenland::init(PISMVars &vars) {
  PetscErrorCode ierr;

  ierr = verbPrintf(2, grid.com,
		    "* Initializing SeaRISE-Greenland atmosphere model based on the Fausto et al (2009)\n"
		    "  air temperature parameterization and using stored time-independent precipitation...\n"); CHKERRQ(ierr);
  
  reference =
    "R. S. Fausto, A. P. Ahlstrom, D. V. As, C. E. Boggild, and S. J. Johnsen, 2009. "
    "A new present-day temperature parameterization for Greenland. J. Glaciol. 55 (189), 95-105.";

  ierr = PAYearlyCycle::init(vars); CHKERRQ(ierr);

  // initialize pointers to fields the parameterization depends on:
  surfelev = dynamic_cast<IceModelVec2S*>(vars.get("surface_altitude"));
  if (!surfelev) SETERRQ(1, "ERROR: surface_altitude is not available");

  lat = dynamic_cast<IceModelVec2S*>(vars.get("latitude"));
  if (!lat) SETERRQ(1, "ERROR: latitude is not available");

  lon = dynamic_cast<IceModelVec2S*>(vars.get("longitude"));
  if (!lon) SETERRQ(1, "ERROR: longitude is not available");

  ierr = PISMOptionsIsSet("-paleo_precip", paleo_precipitation_correction); CHKERRQ(ierr);

  if (paleo_precipitation_correction) {
    PetscTruth dTforcing_set;
    char dT_file[PETSC_MAX_PATH_LEN];

    ierr = PetscOptionsString("-dTforcing", "Specifies the air temperature offsets file",
			      "", "",
			      dT_file, PETSC_MAX_PATH_LEN, &dTforcing_set); CHKERRQ(ierr);
    if (!dTforcing_set) {
      ierr = PetscPrintf(grid.com, "ERROR: option -paleo_precip requires -dTforcing.\n"); CHKERRQ(ierr);
      PISMEnd();
    }

    ierr = verbPrintf(2, grid.com, 
      "  reading delta T data from forcing file %s for -paleo_precip actions ...\n",
      dT_file);  CHKERRQ(ierr);

    dTforcing = new Timeseries(grid.com, grid.rank, "delta_T", "t");
    ierr = dTforcing->set_units("Celsius", ""); CHKERRQ(ierr);
    ierr = dTforcing->set_dimension_units("years", ""); CHKERRQ(ierr);
    ierr = dTforcing->set_attr("long_name", "near-surface air temperature offsets");
             CHKERRQ(ierr);
    ierr = dTforcing->read(dT_file); CHKERRQ(ierr);
  }

  return 0;
}

PetscErrorCode PA_SeaRISE_Greenland::mean_precip(IceModelVec2S &result) {
  PetscErrorCode ierr;

  ierr = PAYearlyCycle::mean_precip(result); CHKERRQ(ierr);

  if ((dTforcing != NULL) && paleo_precipitation_correction) {
    string history = "added the paleo-precipitation correction\n" + result.string_attr("history");

    PetscReal precipexpfactor = config.get("precip_exponential_factor_for_temperature");
    ierr = result.scale(exp( precipexpfactor * (*dTforcing)(t + 0.5 * dt) )); CHKERRQ(ierr);

    ierr = result.set_attr("history", history); CHKERRQ(ierr);
  }

  return 0;
}

//! \brief Updates mean annual and mean July near-surface air temperatures.
//! Note that the precipitation rate is time-independent and does not need
//! to be updated.
PetscErrorCode PA_SeaRISE_Greenland::update(PetscReal t_years, PetscReal dt_years) {
  PetscErrorCode ierr;

  if (lat->has_attr("missing_at_bootstrap")) {
    ierr = PetscPrintf(grid.com, "PISM ERROR: latitude variable was missing at bootstrap;\n"
      "  SeaRISE-Greenland atmosphere model depends on latitude and would return nonsense!!\n");
      CHKERRQ(ierr);
    PISMEnd();
  }
  if (lon->has_attr("missing_at_bootstrap")) {
    ierr = PetscPrintf(grid.com, "PISM ERROR: longitude variable was missing at bootstrap;\n"
      "  SeaRISE-Greenland atmosphere model depends on longitude and would return nonsense!!\n");
      CHKERRQ(ierr);
    PISMEnd();
  }

  if ((fabs(t_years - t) < 1e-12) &&
      (fabs(dt_years - dt) < 1e-12))
    return 0;

  t  = t_years;
  dt = dt_years;

  const PetscReal 
    d_ma     = config.get("snow_temp_fausto_d_ma"),      // K
    gamma_ma = config.get("snow_temp_fausto_gamma_ma"),  // K m-1
    c_ma     = config.get("snow_temp_fausto_c_ma"),      // K (degN)-1
    kappa_ma = config.get("snow_temp_fausto_kappa_ma"),  // K (degW)-1
    d_mj     = config.get("snow_temp_fausto_d_mj"),      // SAME UNITS as for _ma ...
    gamma_mj = config.get("snow_temp_fausto_gamma_mj"),
    c_mj     = config.get("snow_temp_fausto_c_mj"),
    kappa_mj = config.get("snow_temp_fausto_kappa_mj");
  
  PetscScalar **lat_degN, **lon_degE, **h;
  ierr = surfelev->get_array(h);   CHKERRQ(ierr);
  ierr = lat->get_array(lat_degN); CHKERRQ(ierr);
  ierr = lon->get_array(lon_degE); CHKERRQ(ierr);
  ierr = temp_ma.begin_access();  CHKERRQ(ierr);
  ierr = temp_mj.begin_access();  CHKERRQ(ierr);

  for (PetscInt i = grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j<grid.ys+grid.ym; ++j) {
      temp_ma(i,j) = d_ma + gamma_ma * h[i][j] + c_ma * lat_degN[i][j]
                       + kappa_ma * (-lon_degE[i][j]);
      temp_mj(i,j) = d_mj + gamma_mj * h[i][j] + c_mj * lat_degN[i][j]
                       + kappa_mj * (-lon_degE[i][j]);
    }
  }
  
  ierr = surfelev->end_access();   CHKERRQ(ierr);
  ierr = lat->end_access(); CHKERRQ(ierr);
  ierr = lon->end_access(); CHKERRQ(ierr);
  ierr = temp_ma.end_access();  CHKERRQ(ierr);
  ierr = temp_mj.end_access();  CHKERRQ(ierr);

  return 0;
}
