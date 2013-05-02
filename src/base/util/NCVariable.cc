// Copyright (C) 2009, 2010, 2011, 2012, 2013 Constantine Khroulev and Ed Bueler
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
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

#include <algorithm>
#include <sstream>
#include <set>
#include <assert.h>

#include "NCVariable.hh"
#include "NCSpatialVariable.hh"
#include "PIO.hh"
#include "pism_options.hh"
#include "IceGrid.hh"
#include "LocalInterpCtx.hh"
#include "PISMTime.hh"

NCVariable::NCVariable(PISMUnitSystem system)
  : m_units(system), m_glaciological_units(system) {
  reset();
}

//! Reset all the attributes.
PetscErrorCode NCVariable::reset() {

  m_units.reset();
  m_glaciological_units.reset();

  strings.clear();
  short_name = "unnamed_variable";
  // long_name is unset
  // standard_name is unset
  // pism_intent is unset
  // coordinates is unset

  doubles.clear();
  // valid_min and valid_max are unset

  ndims = 0;

  return 0;
}

//! Initialize a NCVariable instance.
void NCVariable::init(string name, MPI_Comm c, PetscMPIInt r) {
  short_name = name;

  com = c;
  rank = r;
}

int NCVariable::get_ndims() const {
  return ndims;
}


//! \brief Read attributes from a file.
PetscErrorCode NCVariable::read_attributes(const PIO &nc) {
  PetscErrorCode ierr;
  bool variable_exists, found_by_std_name;
  string name_found;
  int nattrs;

  ierr = nc.inq_var(short_name, strings["standard_name"], variable_exists,
                    name_found, found_by_std_name); CHKERRQ(ierr);

  if (!variable_exists) {
    ierr = PetscPrintf(com,
		       "PISM ERROR: variable %s was not found in %s.\n"
		       "            Exiting...\n",
		       short_name.c_str(), nc.inq_filename().c_str()); CHKERRQ(ierr);
    PISMEnd();
  }

  strings.clear();
  doubles.clear();

  ierr = nc.inq_nattrs(name_found, nattrs); CHKERRQ(ierr);

  for (int j = 0; j < nattrs; ++j) {
    string attname;
    PISM_IO_Type nctype;
    ierr = nc.inq_attname(name_found, j, attname); CHKERRQ(ierr);
    ierr = nc.inq_atttype(name_found, attname, nctype); CHKERRQ(ierr);

    if (nctype == PISM_CHAR) {
      string value;
      ierr = nc.get_att_text(name_found, attname, value); CHKERRQ(ierr);

      strings[attname] = value;
    } else {
      vector<double> values;

      ierr = nc.get_att_double(name_found, attname, values); CHKERRQ(ierr);
      doubles[attname] = values;
    }
  } // end of for (int j = 0; j < nattrs; ++j)

  return 0;
}


//! Set the internal units.
/*! Units should not be set by accessing the `strings` member directly. This
  method also checks if `new_units` are valid and initializes the `units` structure.
 */
PetscErrorCode NCVariable::set_units(string new_units) {
  strings["units"] = new_units;
  strings["glaciological_units"] = new_units;

  if (m_units.parse(new_units) != 0) {
    SETERRQ2(com, 1, "PISM ERROR: NCVariable '%s': unknown or invalid units specification '%s'.",
	     short_name.c_str(), new_units.c_str());
  }

  // Set the glaciological units too:
  m_glaciological_units = m_units;

  return 0;
}

//! Set the glaciological (output) units.
/*! These units are used for output (if write_in_glaciological_units is set)
  and for standard out reports.

  `glaciological_units` should not be set by accessing the `strings` member
  directly. This method also checks if `new_units` are valid and compatible
  with the internal units.
 */
PetscErrorCode NCVariable::set_glaciological_units(string new_units) {
  string &units_string = strings["units"];

  PISMUnitSystem unit_system = m_units.get_system();

  // Save the human-friendly version of the string; this is to avoid getting
  // things like '3.16887646408185e-08 meter second-1' instead of 'm year-1'
  // (and thus violating the CF conventions).
  strings["glaciological_units"] = new_units;

  /*!
    \note This method finds the string "since" in the units_string and
    terminates it on the first 's' of "since", if this sub-string was found.
    This is done to ignore the reference date in the time units string (the
    reference date specification always starts with this word).
  */
  int n = (int)new_units.find("since");
  if (n != -1)
    new_units.resize(n);

  if (m_glaciological_units.parse(new_units) != 0) {
    SETERRQ2(com, 1, "PISM ERROR: NCVariable '%s': unknown or invalid units specification '%s'.",
	     short_name.c_str(), new_units.c_str());
  }

  if (ut_are_convertible(m_units.get(), m_glaciological_units.get()) == 0) {
    SETERRQ3(com, 1, "PISM ERROR: NCVariable '%s': attempted to set glaciological units to '%s', which is not compatible with '%s'.\n",
	     short_name.c_str(), new_units.c_str(), units_string.c_str());
  }

  return 0;
}

//! \brief Resets metadata.
PetscErrorCode NCSpatialVariable::reset() {
  NCVariable::reset();

  time_independent = false;
  strings["coordinates"] = "lat lon";
  strings["grid_mapping"] = "mapping";

  if (grid)
    variable_order = grid->config.get_string("output_variable_order");
  else
    variable_order = "xyz";

  return 0;
}

NCSpatialVariable::NCSpatialVariable(PISMUnitSystem system)
  : NCVariable(system) {
  grid = NULL;
  dimensions.clear();
  dimensions["x"] = "x";
  dimensions["y"] = "y";
  dimensions["t"] = "t";        // will be overriden later

  x_attrs["axis"]          = "X";
  x_attrs["long_name"]     = "X-coordinate in Cartesian system";
  x_attrs["standard_name"] = "projection_x_coordinate";
  x_attrs["units"]         = "m";

  y_attrs["axis"]          = "Y";
  y_attrs["long_name"]     = "Y-coordinate in Cartesian system";
  y_attrs["standard_name"] = "projection_y_coordinate";
  y_attrs["units"]         = "m";

  z_attrs["axis"]          = "Z";
  z_attrs["long_name"]     = "Z-coordinate in Cartesian system";
  z_attrs["units"]         = "m";
  z_attrs["positive"]      = "up";

  reset();
}

NCSpatialVariable::NCSpatialVariable(const NCSpatialVariable &other)
  : NCVariable(other) {
  dimensions       = other.dimensions;
  x_attrs          = other.x_attrs;
  y_attrs          = other.y_attrs;
  z_attrs          = other.z_attrs;
  time_independent = other.time_independent;
  variable_order   = other.variable_order;
  nlevels          = other.nlevels;
  zlevels          = other.zlevels;
  grid             = other.grid;
}

NCSpatialVariable::~NCSpatialVariable() {
  // empty
}

void NCSpatialVariable::init_2d(string name, IceGrid &g) {
  vector<double> z(1);

  init_3d(name, g, z);
}

//! \brief Initialize a NCSpatialVariable instance.
void NCSpatialVariable::init_3d(string name, IceGrid &g, vector<double> &z_levels) {
  NCVariable::init(name, g.com, g.rank);
  short_name = name;
  grid = &g;

  nlevels = PetscMax((int)z_levels.size(), 1); // to make sure we have at least one level

  zlevels = z_levels;

  dimensions["t"] = grid->config.get_string("time_dimension_name");
  if (nlevels > 1) {
    dimensions["z"] = "z";      // default; can be overridden easily
    ndims = 3;
  } else {
    ndims = 2;
  }

  variable_order = grid->config.get_string("output_variable_order");
}

void NCSpatialVariable::set_levels(const vector<double> &levels) {
  zlevels = levels;
  nlevels = (int)zlevels.size();
}

//! Read a variable from a file into a \b global Vec v.
/*! This also converts the data from input units to internal units if needed.
 */
PetscErrorCode NCSpatialVariable::read(const PIO &nc, unsigned int time, Vec v) {
  PetscErrorCode ierr;

  if (grid == NULL)
    SETERRQ(com, 1, "NCVariable::read: grid is NULL.");

  // Find the variable:
  string name_found;
  bool found_by_standard_name = false, variable_exists = false;
  ierr = nc.inq_var(short_name, strings["standard_name"],
                    variable_exists, name_found, found_by_standard_name); CHKERRQ(ierr);

  if (!variable_exists) {
    ierr = PetscPrintf(com,
                       "PISM ERROR: Can't find '%s' (%s) in '%s'.\n",
		       short_name.c_str(),
		       strings["standard_name"].c_str(), nc.inq_filename().c_str());
    CHKERRQ(ierr);
    PISMEnd();
  }

  // Sanity check: the variable in an input file should have the expected
  // number of spatial dimensions.
  {
    // Set of spatial dimensions this field has.
    std::set<int> axes;
    axes.insert(X_AXIS);
    axes.insert(Y_AXIS);
    if (dimensions["z"].empty() == false) {
      axes.insert(Z_AXIS);
    }

    vector<string> input_dims;
    int input_ndims = 0;                 // number of spatial dimensions (input file)
    size_t matching_dim_count = 0; // number of matching dimensions

    ierr = nc.inq_vardims(name_found, input_dims);
    vector<string>::iterator j = input_dims.begin();
    while (j != input_dims.end()) {
      AxisType tmp;
      ierr = nc.inq_dimtype(*j, tmp); CHKERRQ(ierr);

      if (tmp != T_AXIS)
        ++input_ndims;

      if (axes.find(tmp) != axes.end())
        ++matching_dim_count;

      ++j;
    }


    if (axes.size() != matching_dim_count) {

      // Join input dimension names:
      j = input_dims.begin();
      string tmp = *j++;
      while (j != input_dims.end())
        tmp += string(", ") + *j++;

      // Print the error message and stop:
      PetscPrintf(com,
                  "PISM ERROR: found the %dD variable %s(%s) in '%s' while trying to read\n"
                  "            '%s' ('%s'), which is %d-dimensional.\n",
                  input_ndims, name_found.c_str(), tmp.c_str(), nc.inq_filename().c_str(),
                  short_name.c_str(), strings["long_name"].c_str(),
                  static_cast<int>(axes.size()));
      PISMEnd();

    }
  }
  ierr = nc.get_vec(grid, name_found, nlevels, time, v); CHKERRQ(ierr);

  bool input_has_units;
  PISMUnit input_units(m_units.get_system());

  ierr = nc.inq_units(name_found, input_has_units, input_units); CHKERRQ(ierr);

  if ( has("units") && (!input_has_units) ) {
    string &units_string = strings["units"],
      &long_name = strings["long_name"];
    ierr = verbPrintf(2, com,
		      "PISM WARNING: Variable '%s' ('%s') does not have the units attribute.\n"
		      "              Assuming that it is in '%s'.\n",
		      short_name.c_str(), long_name.c_str(),
		      units_string.c_str()); CHKERRQ(ierr);
    input_units = m_units;
  }

  // Convert data:
  ierr = change_units(v, input_units, m_units); CHKERRQ(ierr);

  return 0;
}

//! \brief Write a \b global Vec `v` to a variable.
/*!
  Defines a variable and converts the units if needed.
 */
PetscErrorCode NCSpatialVariable::write(const PIO &nc, PISM_IO_Type nctype,
                                        bool write_in_glaciological_units, Vec v) {
  PetscErrorCode ierr;

  // find or define the variable
  string name_found;
  bool exists, found_by_standard_name;
  ierr = nc.inq_var(short_name, strings["standard_name"],
                    exists, name_found, found_by_standard_name); CHKERRQ(ierr);

  if (!exists) {
    ierr = define(nc, nctype, write_in_glaciological_units); CHKERRQ(ierr);
    name_found = short_name;
  }

  if (write_in_glaciological_units) {
    ierr = change_units(v, m_units, m_glaciological_units); CHKERRQ(ierr);
  }

  // Actually write data:
  ierr = nc.put_vec(grid, name_found, nlevels, v); CHKERRQ(ierr);

  if (write_in_glaciological_units) {
    ierr = change_units(v, m_glaciological_units, m_units); CHKERRQ(ierr); // restore the units
  }

  return 0;
}

//! \brief Regrid from a NetCDF file into a \b global Vec `v`.
/*!
  \li stops if critical == true and the variable was not found
  \li sets `v` to `default_value` if `set_default_value` == true and the variable was not found
 */
PetscErrorCode NCSpatialVariable::regrid(const PIO &nc, LocalInterpCtx *lic,
                                         bool critical, bool set_default_value,
                                         PetscScalar default_value, Vec v) {
  PetscErrorCode ierr;

  assert(grid != NULL);

  // Find the variable
  bool exists, found_by_standard_name;
  string name_found;
  ierr = nc.inq_var(short_name, strings["standard_name"],
                    exists, name_found, found_by_standard_name); CHKERRQ(ierr);

  if (!exists) {		// couldn't find the variable
    if (critical) {		// if it's critical, print an error message and stop
      ierr = PetscPrintf(com,
			"PISM ERROR: Can't find '%s' in the regridding file '%s'.\n",
			 short_name.c_str(), nc.inq_filename().c_str());
      CHKERRQ(ierr);
      PISMEnd();
    }

    string spacer(short_name.size(), ' ');

    if (set_default_value) {	// if it's not and we have a default value, set it
      double tmp;

      cv_converter *c = m_glaciological_units.get_converter_from(m_units);
      assert(c != NULL);
      tmp = cv_convert_double(c, default_value);
      cv_free(c);

      ierr = verbPrintf(2, com,
			"  absent %s / %-10s\n"
                        "         %s \\ not found; using default constant %7.2f (%s)\n",
			short_name.c_str(),
			strings["long_name"].c_str(),
			spacer.c_str(), tmp,
			strings["glaciological_units"].c_str());
      CHKERRQ(ierr);
      ierr = VecSet(v, default_value); CHKERRQ(ierr);
    } else {			// otherwise leave it alone
      ierr = verbPrintf(2, com,
			"  absent %s / %-10s\n"
                        "         %s \\ not found; continuing without setting it\n",
			short_name.c_str(),
			strings["long_name"].c_str(), spacer.c_str());
      CHKERRQ(ierr);
    }
  } else {			// the variable was found successfully
    ierr = nc.regrid_vec(grid, name_found, zlevels, lic, v); CHKERRQ(ierr);

    // Now we need to get the units string from the file and convert the units,
    // because check_range and report_range expect the data to be in PISM (MKS)
    // units.

    bool input_has_units;
    PISMUnit input_units(m_units.get_system());

    ierr = nc.inq_units(name_found, input_has_units, input_units); CHKERRQ(ierr);

    if (input_has_units == false) {
      input_units = m_units;
      if (get_string("units") != "") {
        ierr = verbPrintf(2, com,
                          "PISM WARNING: Variable '%s' ('%s') does not have the units attribute.\n"
                          "              Assuming that it is in '%s'.\n",
                          short_name.c_str(),
                          strings["long_name"].c_str(),
                          strings["units"].c_str()); CHKERRQ(ierr);
      }
    }

    // Convert data:
    ierr = change_units(v, input_units, m_units); CHKERRQ(ierr);

    // Read the valid range info:
    ierr = read_valid_range(nc, name_found); CHKERRQ(ierr);

    // Check the range and warn the user if needed:
    ierr = check_range(nc.inq_filename(), v); CHKERRQ(ierr);

    if (lic->report_range) {
      // We can report the success, and the range now:
      ierr = verbPrintf(2, com, "  FOUND ");
      ierr = report_range(v, found_by_standard_name); CHKERRQ(ierr);
    }
  } // end of if(exists)

  return 0;
}



//! Read the valid range information from a file.
/*! Reads `valid_min`, `valid_max` and `valid_range` attributes; if \c
    valid_range is found, sets the pair `valid_min` and `valid_max` instead.
 */
PetscErrorCode NCVariable::read_valid_range(const PIO &nc, string name) {
  string input_units_string;
  PISMUnit input_units(m_units.get_system());
  vector<double> bounds;
  int ierr;

  // Never reset valid_min/max if any of them was set internally.
  if (has("valid_min") || has("valid_max"))
    return 0;

  // Read the units: The following code ignores the units in the input file if
  // a) they are absent :-) b) they are invalid c) they are not compatible with
  // internal units.
  ierr = nc.get_att_text(name, "units", input_units_string); CHKERRQ(ierr);

  if (input_units.parse(input_units_string) != 0)
    input_units = m_units;

  cv_converter *c = m_units.get_converter_from(input_units);
  if (c == NULL) {
    c = cv_get_trivial();
  }

  ierr = nc.get_att_double(name, "valid_range", bounds); CHKERRQ(ierr);
  if (bounds.size() == 2) {		// valid_range is present
    this->set("valid_min", cv_convert_double(c, bounds[0]));
    this->set("valid_max", cv_convert_double(c, bounds[1]));
  } else {			// valid_range has the wrong length or is missing
    ierr = nc.get_att_double(name, "valid_min", bounds); CHKERRQ(ierr);
    if (bounds.size() == 1) {		// valid_min is present
      this->set("valid_min", cv_convert_double(c, bounds[0]));
    }

    ierr = nc.get_att_double(name, "valid_max", bounds); CHKERRQ(ierr);
    if (bounds.size() == 1) {		// valid_max is present
      this->set("valid_max", cv_convert_double(c, bounds[0]));
    }
  }

  cv_free(c);

  return 0;
}

//! Converts `v` from internal to glaciological units.
PetscErrorCode NCSpatialVariable::to_glaciological_units(Vec v) {
  return change_units(v, m_units, m_glaciological_units);
}

//! Converts `v` from the units corresponding to `from` to the ones corresponding to `to`.
/*!
  Does nothing if this transformation is trivial.
 */
PetscErrorCode NCSpatialVariable::change_units(Vec v, PISMUnit &from, PISMUnit &to) {
  PetscErrorCode ierr;
  string from_name, to_name;

  from_name = from.format();
  to_name   = to.format();

  // Get the converter:
  cv_converter *c = to.get_converter_from(from);
  if (c == NULL) {              // can't convert
    ierr = PetscPrintf(com,
                       "PISM ERROR: NCSpatialVariable '%s': attempted to convert data from '%s' to '%s'.\n",
                       short_name.c_str(), from_name.c_str(), to_name.c_str());
    PISMEnd();
  }

  PetscInt data_size;
  PetscScalar *data;
  ierr = VecGetLocalSize(v, &data_size); CHKERRQ(ierr);

  ierr = VecGetArray(v, &data); CHKERRQ(ierr);
  cv_convert_doubles(c, data, data_size, data);
  ierr = VecRestoreArray(v, &data); CHKERRQ(ierr);

  cv_free(c);
  return 0;
}

//! Write variable attributes to a NetCDF file.
/*!

  \li if write_in_glaciological_units == true, "glaciological_units" are
  written under the name "units" plus the valid range is written in
  glaciological units.

  \li if both valid_min and valid_max are set, then valid_range is written
  instead of the valid_min, valid_max pair.
 */
PetscErrorCode NCVariable::write_attributes(const PIO &nc, PISM_IO_Type nctype,
					    bool write_in_glaciological_units) const {
  int ierr;

  ierr = nc.redef(); CHKERRQ(ierr);

  // units, valid_min, valid_max and valid_range need special treatment:
  if (has("units")) {
    string output_units = get_string("units");

    if (write_in_glaciological_units)
      output_units = get_string("glaciological_units");

    ierr = nc.put_att_text(short_name, "units", output_units); CHKERRQ(ierr);
  }

  vector<double> bounds(2);
  double fill_value = 0.0;

  if (has("_FillValue")) {
    fill_value = get("_FillValue");
  }

  // We need to save valid_min, valid_max and valid_range in the units
  // matching the ones in the output.
  if (write_in_glaciological_units) {

    cv_converter *c = m_glaciological_units.get_converter_from(m_units);
    assert(c != NULL);

    bounds[0]  = cv_convert_double(c, get("valid_min"));
    bounds[1]  = cv_convert_double(c, get("valid_max"));
    fill_value = cv_convert_double(c, fill_value);

    cv_free(c);
  } else {
    bounds[0] = get("valid_min");
    bounds[1] = get("valid_max");
  }

  if (has("_FillValue")) {
    ierr = nc.put_att_double(short_name, "_FillValue", nctype, fill_value); CHKERRQ(ierr);
  }

  if (has("valid_min") && has("valid_max")) {
    ierr = nc.put_att_double(short_name, "valid_range", nctype, bounds);
  } else if (has("valid_min")) {
    ierr = nc.put_att_double(short_name, "valid_min",   nctype, bounds[0]);
  } else if (has("valid_max")) {
    ierr = nc.put_att_double(short_name, "valid_max",   nctype, bounds[1]);
  }

  CHKERRQ(ierr);

  // Write text attributes:
  map<string, string>::const_iterator i;
  for (i = strings.begin(); i != strings.end(); ++i) {
    string name  = i->first, value = i->second;

    if (name == "units" || name == "glaciological_units" || value.empty())
      continue;

    ierr = nc.put_att_text(short_name, name, value); CHKERRQ(ierr);
  }

  // Write double attributes:
  map<string, vector<double> >::const_iterator j;
  for (j = doubles.begin(); j != doubles.end(); ++j) {
    string name  = j->first;
    vector<double> values = j->second;

    if (name == "valid_min" ||
	name == "valid_max" ||
	name == "valid_range" ||
	name == "_FillValue" ||
	values.empty())
      continue;

    ierr = nc.put_att_double(short_name, name, nctype, values); CHKERRQ(ierr);
  }

  return 0;
}


//! Report the range of a \b global Vec `v`.
PetscErrorCode NCSpatialVariable::report_range(Vec v, bool found_by_standard_name) {
  PetscErrorCode ierr;
  PetscReal min, max;

  ierr = VecMin(v, PETSC_NULL, &min); CHKERRQ(ierr);
  ierr = VecMax(v, PETSC_NULL, &max); CHKERRQ(ierr);

  cv_converter *c = m_glaciological_units.get_converter_from(m_units);
  assert(c != NULL);
  min = cv_convert_double(c, min);
  max = cv_convert_double(c, max);
  cv_free(c);

  string spacer(short_name.size(), ' ');

  if (has("standard_name")) {

    if (found_by_standard_name) {
      ierr = verbPrintf(2, com,
			" %s / standard_name=%-10s\n"
                        "         %s \\ min,max = %9.3f,%9.3f (%s)\n",
			short_name.c_str(),
			strings["standard_name"].c_str(), spacer.c_str(), min, max,
			strings["glaciological_units"].c_str()); CHKERRQ(ierr);
    } else {
      ierr = verbPrintf(2, com,
			" %s / WARNING! standard_name=%s is missing, found by short_name\n"
                        "         %s \\ min,max = %9.3f,%9.3f (%s)\n",
			short_name.c_str(),
			strings["standard_name"].c_str(), spacer.c_str(), min, max,
			strings["glaciological_units"].c_str()); CHKERRQ(ierr);
    }

  } else {

    ierr = verbPrintf(2, com,
		      " %s / %-10s\n"
                      "         %s \\ min,max = %9.3f,%9.3f (%s)\n",
		      short_name.c_str(),
		      strings["long_name"].c_str(), spacer.c_str(), min, max,
		      strings["glaciological_units"].c_str()); CHKERRQ(ierr);
  }

  return 0;
}

//! Check if the range of a \b global Vec `v` is in the range specified by valid_min and valid_max attributes.
PetscErrorCode NCSpatialVariable::check_range(string filename, Vec v) {
  PetscScalar min, max;
  PetscErrorCode ierr;
  bool failed = false;

  if (grid == NULL)
    SETERRQ(com, 1, "NCVariable::check_range: grid is NULL.");

  // Vec v is always global here (so VecMin and VecMax work as expected)
  ierr = VecMin(v, PETSC_NULL, &min); CHKERRQ(ierr);
  ierr = VecMax(v, PETSC_NULL, &max); CHKERRQ(ierr);

  string &units_string = strings["units"];

  if (has("valid_min") && has("valid_max")) {
    double valid_min = get("valid_min"),
      valid_max = get("valid_max");
    if ((min < valid_min) || (max > valid_max)) {
      ierr = PetscPrintf(com,
                         "PISM ERROR: some values of '%s' are outside the valid range [%f, %f] (%s).\n",
                         short_name.c_str(), valid_min, valid_max, units_string.c_str()); CHKERRQ(ierr);
      failed = true;
    }

  } else if (has("valid_min")) {
    double valid_min = get("valid_min");
    if (min < valid_min) {
      ierr = PetscPrintf(com,
                         "PISM ERROR: some values of '%s' are less than the valid minimum (%f %s).\n",
                         short_name.c_str(), valid_min, units_string.c_str()); CHKERRQ(ierr);
      failed = true;
    }

  } else if (has("valid_max")) {
    double valid_max = get("valid_max");
    if (max > valid_max) {
      ierr = PetscPrintf(com,
                         "PISM ERROR: some values of '%s' are greater than the valid maximum (%f %s).\n",
                         short_name.c_str(), valid_max, units_string.c_str()); CHKERRQ(ierr);
      failed = true;
    }
  }

  if (failed == true) {
    PetscPrintf(com,
                "            Please inspect variable '%s' in '%s' and replace offending entries.\n"
                "            PISM will stop now.\n",
                short_name.c_str(), filename.c_str());
    PISMEnd();
  }

  return 0;
}

//! \brief Define dimensions a variable depends on.
PetscErrorCode NCSpatialVariable::define_dimensions(const PIO &nc) {
  PetscErrorCode ierr;
  string dimname;
  bool exists;

  // x
  dimname = dimensions["x"];
  ierr = nc.inq_dim(dimname, exists); CHKERRQ(ierr);
  if (!exists) {
    ierr = nc.def_dim(dimname, grid->Mx, x_attrs); CHKERRQ(ierr);
    ierr = nc.put_dim(dimname, grid->x); CHKERRQ(ierr);
  }

  // y
  dimname = dimensions["y"];
  ierr = nc.inq_dim(dimname, exists); CHKERRQ(ierr);
  if (!exists) {
    ierr = nc.def_dim(dimname, grid->My, y_attrs); CHKERRQ(ierr);
    ierr = nc.put_dim(dimname, grid->y); CHKERRQ(ierr);
  }

  // z
  dimname = dimensions["z"];
  if (dimname.empty() == false) {
    ierr = nc.inq_dim(dimname, exists); CHKERRQ(ierr);
    if (!exists) {
      ierr = nc.def_dim(dimname, nlevels, z_attrs); CHKERRQ(ierr);
      ierr = nc.put_dim(dimname, zlevels); CHKERRQ(ierr);
    }
  }

  return 0;
}

//! Define a NetCDF variable corresponding to a NCVariable object.
PetscErrorCode NCSpatialVariable::define(const PIO &nc, PISM_IO_Type nctype,
                                         bool write_in_glaciological_units) {
  int ierr;
  vector<string> dims;
  bool exists;

  ierr = nc.inq_var(short_name, exists); CHKERRQ(ierr);
  if (exists)
    return 0;

  ierr = define_dimensions(nc); CHKERRQ(ierr);

  // "..._bounds" should be stored with grid corners (corresponding to
  // the "z" dimension here) last, so we override the variable storage
  // order here
  if (ends_with(short_name, "_bounds") && variable_order == "zyx")
    variable_order = "yxz";

  string x = dimensions["x"],
    y = dimensions["y"],
    z = dimensions["z"],
    t = dimensions["t"];

  ierr = nc.redef(); CHKERRQ(ierr);

  if (!time_independent) {
    dims.push_back(t);
  }

  // Use t,x,y,z(zb) variable order: it is weird, but matches the in-memory
  // storage order and so is *a lot* faster.
  if (variable_order == "xyz") {
    dims.push_back(x);
    dims.push_back(y);
  }

  // Use the t,y,x,z variable order: also weird, somewhat slower, but 2D fields
  // are stored in the "natural" order.
  if (variable_order == "yxz") {
    dims.push_back(y);
    dims.push_back(x);
  }

  if (z.empty() == false) {
    dims.push_back(z);
  }

  // Use the t,z(zb),y,x variables order: more natural for plotting and post-processing,
  // but requires transposing data while writing and is *a lot* slower.
  if (variable_order == "zyx") {
    dims.push_back(y);
    dims.push_back(x);
  }

  ierr = nc.def_var(short_name, nctype, dims); CHKERRQ(ierr);

  ierr = write_attributes(nc, nctype, write_in_glaciological_units); CHKERRQ(ierr);

  return 0;
}

//! Checks if an attribute is present. Ignores empty strings, except
//! for the "units" attribute.
bool NCVariable::has(string name) const {

  map<string,string>::const_iterator j = strings.find(name);
  if (j != strings.end()) {
    if (name != "units" && (j->second).empty())
      return false;

    return true;
  }

  if (doubles.find(name) != doubles.end())
    return true;

  return false;
}

//! Set a scalar attribute to a single (scalar) value.
void NCVariable::set(string name, double value) {
  doubles[name] = vector<double>(1, value);
}

//! Get a single-valued scalar attribute.
double NCVariable::get(string name) const {
  map<string,vector<double> >::const_iterator j = doubles.find(name);
  if (j != doubles.end())
    return (j->second)[0];
  else
    return GSL_NAN;
}

//! Set a string attribute.
void NCVariable::set_string(string name, string value) {
  if (name == "short_name") {
    short_name = name;
    return;
  }

  strings[name] = value;
}

//! Get a string attribute.
/*!
 * Returns an empty string if an attribute is not set.
 */
string NCVariable::get_string(string name) const {
  if (name == "short_name") return short_name;

  map<string,string>::const_iterator j = strings.find(name);
  if (j != strings.end())
    return j->second;
  else
    return string();
}

//! \brief Check if a value `a` is in the valid range defined by `valid_min`
//! and `valid_max` attributes.
bool NCVariable::is_valid(PetscScalar a) const {

  if (has("valid_min") && has("valid_max"))
    return (a >= get("valid_min")) && (a <= get("valid_max"));

  if (has("valid_min"))
    return a >= get("valid_min");

  if (has("valid_max"))
    return a <= get("valid_max");

  return true;
}

NCConfigVariable::NCConfigVariable(PISMUnitSystem unit_system)
  : NCVariable(unit_system), m_unit_system(unit_system) {
  options_left_set = false;
  PISMOptionsIsSet("-options_left", options_left_set);
  m_unit_system = unit_system;
}

NCConfigVariable::~NCConfigVariable() {
  warn_about_unused_parameters();
}


PetscErrorCode NCConfigVariable::read(string filename) {
  PetscErrorCode ierr;
  PIO nc(com, rank, "netcdf3", m_unit_system); // OK to use netcdf3

  ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);

  ierr = this->read(nc); CHKERRQ(ierr);

  ierr = nc.close(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode NCConfigVariable::write(string filename) {
  PetscErrorCode ierr;
  PIO nc(com, rank, "netcdf3", m_unit_system); // OK to use netcdf3

  ierr = nc.open(filename, PISM_WRITE, true); CHKERRQ(ierr);

  ierr = this->write(nc); CHKERRQ(ierr);

  ierr = nc.close(); CHKERRQ(ierr);

  return 0;
}

//! Read boolean flags and double parameters from a NetCDF file.
/*!
  Erases all the present parameters before reading.
 */
PetscErrorCode NCConfigVariable::read(const PIO &nc) {

  PetscErrorCode ierr = this->read_attributes(nc); CHKERRQ(ierr);

  config_filename = nc.inq_filename();

  return 0;
}

//! Write a config variable to a file (with all its attributes).
PetscErrorCode NCConfigVariable::write(const PIO &nc) {
  PetscErrorCode ierr;
  bool variable_exists;

  ierr = nc.inq_var(short_name, variable_exists); CHKERRQ(ierr);

  if (!variable_exists) {
    ierr = define(nc, PISM_BYTE, false); CHKERRQ(ierr);
  } else {
    ierr = write_attributes(nc, PISM_DOUBLE, false); CHKERRQ(ierr);
  }

  return 0;
}

//! Define a configuration NetCDF variable.
PetscErrorCode NCConfigVariable::define(const PIO &nc, PISM_IO_Type type, bool) {
  int ierr;
  bool exists;

  ierr = nc.inq_var(short_name, exists); CHKERRQ(ierr);
  if (exists)
    return 0;

  ierr = nc.redef(); CHKERRQ(ierr);

  vector<string> dims;
  ierr = nc.def_var(short_name, type, dims); CHKERRQ(ierr);

  ierr = write_attributes(nc, PISM_DOUBLE, false); CHKERRQ(ierr);

  return 0;
}

double NCConfigVariable::get_quiet(string name) const {
  if (doubles.find(name) != doubles.end()) {
    return NCVariable::get(name);
  } else {
    PetscPrintf(com, "PISM ERROR: parameter '%s' is unset. (Parameters read from '%s'.)\n",
		name.c_str(), config_filename.c_str());
    PISMEnd();
  }

  return 0;			// can't happen
}

string NCConfigVariable::get_string_quiet(string name) const {
  map<string,string>::const_iterator j = strings.find(name);
  if (j != strings.end())
    return j->second;
  else {
    PetscPrintf(com,
		"PISM ERROR: Parameter '%s' was not set. (Read from '%s'.)\n",
		name.c_str(), config_filename.c_str());
    PISMEnd();
  }

  return string();		// will never happen
}

bool NCConfigVariable::get_flag_quiet(string name) const {
  map<string,string>::const_iterator j = strings.find(name);
  if (j != strings.end()) {

    const string &value = j->second;

    if ((value == "false") ||
	(value == "no") ||
	(value == "off"))
      return false;

    if ((value == "true") ||
	(value == "yes") ||
	(value == "on"))
      return true;

    PetscPrintf(com,
		"PISM ERROR: Parameter '%s' (%s) cannot be interpreted as a boolean.\n"
		"            Please make sure that it is set to one of 'true', 'yes', 'on', 'false', 'no', 'off'.\n",
		name.c_str(), value.c_str());
    PISMEnd();
  }

  PetscPrintf(com,
	      "PISM ERROR: Parameter '%s' was not set. (Read from '%s'.)\n",
	      name.c_str(), config_filename.c_str());
  PISMEnd();

  return true;			// will never happen
}


//! Returns a `double` parameter. Stops if it was not found.
double NCConfigVariable::get(string name) const {
  if (options_left_set)
    parameters_used.insert(name);

  return this->get_quiet(name);
}

double NCConfigVariable::get(string name, string u1, string u2) const {
  // always use get() (*not* _quiet) here
  return m_unit_system.convert(this->get(name),  u1.c_str(),  u2.c_str());
}


//! Returns a boolean flag by name. Unset flags are treated as if they are set to 'false'.
/*!
  Strings "false", "no", "off" are interpreted as 'false'; "true", "on", "yes" -- as 'true'.

  Any other string produces an error.
 */
bool NCConfigVariable::get_flag(string name) const {
  if (options_left_set)
    parameters_used.insert(name);

  return this->get_flag_quiet(name);
}

//! \brief Get a string attribute by name.
string NCConfigVariable::get_string(string name) const {
  if (options_left_set)
    parameters_used.insert(name);

  return this->get_string_quiet(name);
}

//! Set a value of a boolean flag.
void NCConfigVariable::set_flag(string name, bool value) {
  if (value)
    strings[name] = "true";
  else
    strings[name] = "false";
}

//! Write attributes to a NetCDF variable. All attributes are equal here.
PetscErrorCode NCConfigVariable::write_attributes(const PIO &nc, PISM_IO_Type nctype,
						  bool /*write_in_glaciological_units*/) const {
  int ierr;

  // Write text attributes:
  map<string, string>::const_iterator i;
  for (i = strings.begin(); i != strings.end(); ++i) {
    const string &name  = i->first,
      &value = i->second;

    if (value.empty()) continue;

    ierr = nc.put_att_text(short_name, name, value); CHKERRQ(ierr);
  }

  // Write double attributes:
  map<string, vector<double> >::const_iterator j;
  for (j = doubles.begin(); j != doubles.end(); ++j) {
    const string &name  = j->first;
    const vector<double> &values = j->second;

    if (values.empty()) continue;

    ierr = nc.put_att_double(short_name, name, nctype, values); CHKERRQ(ierr);
  }

  return 0;
}

//! Get a flag from a command-line option.
/*!
  If called as flag_from_option("foo", "foo"), checks both -foo and -no_foo.

  \li if -foo is set, calls set_flag("foo", true),

  \li if -no_foo is set, calls set_flag("foo", false),

  \li if both are set, prints an error message and stops,

  \li if none, does nothing.

 */
PetscErrorCode NCConfigVariable::flag_from_option(string name, string flag) {
  PetscErrorCode ierr;
  bool foo = false,
    no_foo = false;

  ierr = PISMOptionsIsSet("-" + name, get_string_quiet(flag + "_doc"), foo); CHKERRQ(ierr);
  ierr = PISMOptionsIsSet("-no_" + name, no_foo); CHKERRQ(ierr);

  if (foo && no_foo) {
    PetscPrintf(com, "PISM ERROR: Inconsistent command-line options: both -%s and -no_%s are set.\n",
		name.c_str(), name.c_str());
    PISMEnd();
  }

  if (foo)
    set_flag_from_option(flag, true);

  if (no_foo)
    set_flag_from_option(flag, false);

  return 0;
}

//! Sets a configuration parameter from a command-line option.
/*!
  If called as scalar_from_option("foo", "foo"), checks -foo and calls set("foo", value).

  Does nothing if -foo was not set.

  Note that no unit conversion is performed; parameters should be stored in
  input units and converted as needed. (This allows saving parameters without
  converting again.)
 */
PetscErrorCode NCConfigVariable::scalar_from_option(string name, string parameter) {
  PetscErrorCode ierr;
  PetscReal value = get_quiet(parameter);
  bool flag;

  ierr = PISMOptionsReal("-" + name,
			 get_string_quiet(parameter + "_doc"),
			 value, flag); CHKERRQ(ierr);
  if (flag) {
    this->set_scalar_from_option(parameter, value);
  }

  return 0;
}

PetscErrorCode NCConfigVariable::string_from_option(string name, string parameter) {
  PetscErrorCode ierr;
  string value = get_string_quiet(parameter);
  bool flag;

  ierr = PISMOptionsString("-" + name,
                           get_string_quiet(parameter + "_doc"),
                           value, flag); CHKERRQ(ierr);
  if (flag) {
    this->set_string_from_option(parameter, value);
  }

  return 0;
}

//! \brief Set a keyword parameter from a command-line option.
/*!
 * This sets the parameter "parameter" after checking the "-name" command-line
 * option. This option requires an argument, which has to match one of the
 * keyword given in a comma-separated list "choices_list".
 */
PetscErrorCode NCConfigVariable::keyword_from_option(string name,
                                                     string parameter,
                                                     string choices_list) {
  PetscErrorCode ierr;
  istringstream arg(choices_list);
  std::set<string> choices;     // resolve the name clash: "this->set(...)" vs. "std::set"
  string keyword, tmp;
  bool flag;

  // Split the list:
  while (getline(arg, tmp, ','))
    choices.insert(tmp);

  ierr = PISMOptionsList(com, "-" + name,
                         get_string_quiet(parameter + "_doc"),
			 choices,
                         get_string_quiet(parameter), keyword, flag); CHKERRQ(ierr);

  if (flag) {
    this->set_string_from_option(parameter, keyword);
  }

  return 0;
}

PetscErrorCode NCConfigVariable::set_flag_from_option(string name, bool value) {

  parameters_set.insert(name);

  this->set_flag(name, value);

  return 0;
}

PetscErrorCode NCConfigVariable::set_scalar_from_option(string name, double value) {

  parameters_set.insert(name);

  this->set(name, value);

  return 0;
}

PetscErrorCode NCConfigVariable::set_string_from_option(string name, string value) {

  parameters_set.insert(name);

  this->set_string(name, value);

  return 0;
}

PetscErrorCode NCConfigVariable::set_keyword_from_option(string name, string value) {

  this->set_string_from_option(name, value);

  return 0;
}


//! Print all the attributes of a configuration variable.
PetscErrorCode NCConfigVariable::print(PetscInt vt) const {
  PetscErrorCode ierr;

  ierr = verbPrintf(vt, com, "PISM parameters read from %s:\n",
		    config_filename.c_str());

  // Print text attributes:
  map<string, string>::const_iterator i;
  for (i = strings.begin(); i != strings.end(); ++i) {
    string name  = i->first;
    string value = i->second;

    if (value.empty()) continue;

    ierr = verbPrintf(vt, com, "  %s = \"%s\"\n",
		      name.c_str(), value.c_str()); CHKERRQ(ierr);
  }

  // Print double attributes:
  map<string, vector<double> >::const_iterator j;
  for (j = doubles.begin(); j != doubles.end(); ++j) {
    string name  = j->first;
    vector<double> values = j->second;

    if (values.empty()) continue;

    if ((fabs(values[0]) >= 1.0e7) || (fabs(values[0]) <= 1.0e-4)) {
      // use scientific notation if a number is big or small
      ierr = verbPrintf(vt, com, "  %s = %12.3e\n",
		        name.c_str(), values[0]); CHKERRQ(ierr);
    } else {
      ierr = verbPrintf(vt, com, "  %s = %12.5f\n",
		        name.c_str(), values[0]); CHKERRQ(ierr);
    }

  }

  std::set<string>::const_iterator k;
  string output;

  for (k = parameters_set.begin(); k != parameters_set.end(); ++k) {

    if (ends_with(*k, "_doc"))
      continue;

    if (k == parameters_set.begin())
      output += *k;
    else
      output += string(", ") + (*k);
  }

  if (output.empty() == false) {
    ierr = verbPrintf(vt, com, "PISM flags and parameters set from the command line:\n  %s\n",
                      output.c_str()); CHKERRQ(ierr);
  }

  return 0;
}

//! \brief Returns the name of the file used to initialize the database.
string NCConfigVariable::get_config_filename() const {
  return config_filename;
}

PISMUnitSystem NCConfigVariable::get_unit_system() const {
  return m_unit_system;
}

//! Imports values from the other config variable, silently overwriting present values.
void NCConfigVariable::import_from(const NCConfigVariable &other) {
  map<string, vector<double> >::const_iterator j;
  for (j = other.doubles.begin(); j != other.doubles.end(); ++j) {
    doubles[j->first] = j->second;
    parameters_set.insert(j->first);
  }

  map<string,string>::const_iterator k;
  for (k = other.strings.begin(); k != other.strings.end(); ++k) {
    strings[k->first] = k->second;
    parameters_set.insert(k->first);
  }
}

//! Update values from the other config variable, overwriting present values but avoiding adding new ones.
void NCConfigVariable::update_from(const NCConfigVariable &other) {
  map<string, vector<double> >::iterator j;
  map<string, vector<double> >::const_iterator i;
  for (j = doubles.begin(); j != doubles.end(); ++j) {
    i = other.doubles.find(j->first);
    if (i != other.doubles.end()) {
      j->second = i->second;
    }
  }

  map<string,string>::iterator k;
  map<string,string>::const_iterator m;
  for (k = strings.begin(); k != strings.end(); ++k) {
    m = other.strings.find(k->first);
    if (m != other.strings.end()) {
      k->second = m->second;
    }
  }
}

PetscErrorCode NCConfigVariable::warn_about_unused_parameters() const {
  PetscErrorCode ierr;

  if (options_left_set == false)
    return 0;

  std::set<string>::const_iterator k;
  for (k = parameters_set.begin(); k != parameters_set.end(); ++k) {

    if (ends_with(*k, "_doc"))
      continue;

    if (parameters_used.find(*k) == parameters_used.end()) {
      ierr = verbPrintf(2, com,
                        "PISM WARNING: flag or parameter \"%s\" was set but was not used!\n",
                        k->c_str()); CHKERRQ(ierr);

    }
  }

  return 0;
}

NCTimeseries::NCTimeseries(PISMUnitSystem system)
  : NCVariable(system) {
  // empty
}


//! \brief Initialize the time-series object.
void NCTimeseries::init(string n, string dim_name, MPI_Comm c, PetscMPIInt r) {
  NCVariable::init(n, c, r);
  dimension_name = dim_name;
  ndims = 1;
}

//! Read a time-series variable from a NetCDF file to a vector of doubles.
PetscErrorCode NCTimeseries::read(const PIO &nc, PISMTime *time,
                                  vector<double> &data) {

  PetscErrorCode ierr;
  bool variable_exists;

  // Find the variable:
  string name_found;
  bool found_by_standard_name;
  ierr = nc.inq_var(short_name, strings["standard_name"],
                    variable_exists, name_found, found_by_standard_name); CHKERRQ(ierr);

  if (!variable_exists) {
    ierr = PetscPrintf(com,
                       "PISM ERROR: Can't find '%s' (%s) in '%s'.\n",
		       short_name.c_str(),
		       strings["standard_name"].c_str(), nc.inq_filename().c_str());
    CHKERRQ(ierr);
    PISMEnd();
  }

  vector<string> dims;
  ierr = nc.inq_vardims(name_found, dims); CHKERRQ(ierr);

  if (dims.size() != 1) {
    ierr = PetscPrintf(com,
		       "PISM ERROR: Variable '%s' in '%s' depends on %d dimensions,\n"
		       "            but a time-series variable can only depend on 1 dimension.\n",
		       short_name.c_str(), nc.inq_filename().c_str(), dims.size()); CHKERRQ(ierr);
    PISMEnd();
  }

  dimension_name = dims[0];

  unsigned int length;
  ierr = nc.inq_dimlen(dimension_name, length); CHKERRQ(ierr);

  if (length <= 0) {
    ierr = PetscPrintf(com,
		       "PISM ERROR: Dimension %s has zero (or negative) length!\n",
		       dimension_name.c_str()); CHKERRQ(ierr);
    PISMEnd();
  }

  data.resize(length);		// memory allocation happens here

  ierr = nc.enddef(); CHKERRQ(ierr);

  ierr = nc.get_1d_var(name_found, 0, length, data); CHKERRQ(ierr);

  bool input_has_units;
  string input_units_string;
  PISMUnit input_units(m_units.get_system());

  ierr = nc.get_att_text(name_found, "units", input_units_string); CHKERRQ(ierr);
  input_units_string = time->CF_units_to_PISM_units(input_units_string);

  if (input_units_string.empty() == true) {
    input_has_units = false;
  } else {
    if (input_units.parse(input_units_string) != 0) {
      ierr = PetscPrintf(com,
                         "PISM ERROR: units specification '%s' is unknown or invalid (processing variable '%s').\n",
                         input_units_string.c_str(), short_name.c_str());
      PISMEnd();
    }
    input_has_units = true;
  }
  
  if (has("units") == true && input_has_units == false) {
    string &units_string = strings["units"],
      &long_name = strings["long_name"];
    ierr = verbPrintf(2, com,
		      "PISM WARNING: Variable '%s' ('%s') does not have the units attribute.\n"
		      "              Assuming that it is in '%s'.\n",
		      short_name.c_str(), long_name.c_str(),
		      units_string.c_str()); CHKERRQ(ierr);
    input_units = m_units;
  }

  ierr = change_units(data, input_units, m_units); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode NCTimeseries::get_bounds_name(const PIO &nc, string &result) {
  PetscErrorCode ierr;
  bool exists, found_by_standard_name;
  string name_found;

  ierr = nc.inq_var(short_name, strings["standard_name"],
                    exists, name_found, found_by_standard_name); CHKERRQ(ierr);
  if (exists) {
    ierr = nc.get_att_text(name_found, "bounds", result); CHKERRQ(ierr);
  } else {
    result.clear();
  }

  return 0;
}


//! \brief Report the range of a time-series stored in `data`.
PetscErrorCode NCTimeseries::report_range(vector<double> &data) {
  PetscErrorCode ierr;
  PetscReal min, max;

  min = *min_element(data.begin(), data.end());
  max = *max_element(data.begin(), data.end());

  cv_converter *c = m_glaciological_units.get_converter_from(m_units);
  assert(c != NULL);
  min = cv_convert_double(c, min);
  max = cv_convert_double(c, max);
  cv_free(c);

  string spacer(short_name.size(), ' ');

  ierr = verbPrintf(2, com,
		    "  FOUND  %s / %-60s\n"
                    "         %s \\ min,max = %9.3f,%9.3f (%s)\n",
		    short_name.c_str(),
		    strings["long_name"].c_str(), spacer.c_str(), min, max,
		    strings["glaciological_units"].c_str()); CHKERRQ(ierr);

  return 0;
}

//! Define a NetCDF variable corresponding to a time-series.
PetscErrorCode NCTimeseries::define(const PIO &nc, PISM_IO_Type nctype, bool) {
  PetscErrorCode ierr;

  bool exists;
  ierr = nc.inq_var(short_name, exists); CHKERRQ(ierr);
  if (exists)
    return 0;

  ierr = nc.redef(); CHKERRQ(ierr);

  ierr = nc.inq_dim(dimension_name, exists); CHKERRQ(ierr);
  if (exists == false) {
    map<string,string> tmp;
    ierr = nc.def_dim(dimension_name, PISM_UNLIMITED, tmp); CHKERRQ(ierr);
  }

  ierr = nc.inq_var(short_name, exists); CHKERRQ(ierr);
  if (exists == false) {
    vector<string> dims(1);
    dims[0] = dimension_name;
    ierr = nc.redef(); CHKERRQ(ierr);
    ierr = nc.def_var(short_name, nctype, dims); CHKERRQ(ierr);
  }

  ierr = write_attributes(nc, PISM_FLOAT, true);

  return 0;
}

//! \brief Write a time-series `data` to a file.
PetscErrorCode NCTimeseries::write(const PIO &nc, size_t start,
				   vector<double> &data, PISM_IO_Type nctype) {

  PetscErrorCode ierr;
  bool variable_exists = false;

  ierr = nc.inq_var(short_name, variable_exists); CHKERRQ(ierr);
  if (!variable_exists) {
    ierr = define(nc, nctype, true); CHKERRQ(ierr);
  }

  ierr = nc.enddef(); CHKERRQ(ierr);

  // convert to glaciological units:
  ierr = change_units(data, m_units, m_glaciological_units); CHKERRQ(ierr);

  ierr = nc.put_1d_var(short_name,
		       static_cast<unsigned int>(start),
		       static_cast<unsigned int>(data.size()), data); CHKERRQ(ierr);

  // restore internal units:
  ierr = change_units(data, m_glaciological_units, m_units); CHKERRQ(ierr);
  return 0;
}

//! \brief Write a single value of a time-series to a file.
PetscErrorCode NCTimeseries::write(const PIO &nc, size_t start,
				   double data, PISM_IO_Type nctype) {
  vector<double> tmp(1);
  tmp[0] = data;
  PetscErrorCode ierr = write(nc, start, tmp, nctype); CHKERRQ(ierr);

  return 0;
}

//! Convert `data`.
PetscErrorCode NCTimeseries::change_units(vector<double> &data, PISMUnit &from, PISMUnit &to) {
  PetscErrorCode ierr;
  string from_name, to_name;

  // Get string representations of units:
  from_name = from.format();
  to_name   = to.format();

  // Get the converter:
  cv_converter *c = to.get_converter_from(from);
  if (c == NULL) {              // can't convert
    ierr = PetscPrintf(com,
                       "PISM ERROR: NCTimeseries '%s': attempted to convert data from '%s' to '%s'.\n",
                       short_name.c_str(), from_name.c_str(), to_name.c_str());
    PISMEnd();
  }

  cv_convert_doubles(c, &data[0], data.size(), &data[0]);
  cv_free(c);

  return 0;
}

NCGlobalAttributes::NCGlobalAttributes(PISMUnitSystem system)
  : NCConfigVariable(system) {
  // empty
}


//! \brief Read global attributes from a file.
PetscErrorCode NCGlobalAttributes::read(const PIO &nc) {
  PetscErrorCode ierr;
  int nattrs;

  strings.clear();
  doubles.clear();
  config_filename = nc.inq_filename();

  ierr = nc.inq_nattrs("PISM_GLOBAL", nattrs); CHKERRQ(ierr);

  for (int j = 0; j < nattrs; ++j) {
    string attname;
    PISM_IO_Type nctype;
    ierr = nc.inq_attname("PISM_GLOBAL", j, attname); CHKERRQ(ierr);
    ierr = nc.inq_atttype("PISM_GLOBAL", attname, nctype); CHKERRQ(ierr);

    if (nctype == PISM_CHAR) {
      string value;
      ierr = nc.get_att_text("PISM_GLOBAL", attname, value); CHKERRQ(ierr);

      strings[attname] = value;
    } else {
      vector<double> values;

      ierr = nc.get_att_double("PISM_GLOBAL", attname, values); CHKERRQ(ierr);
      doubles[attname] = values;
    }
  } // end of for (int j = 0; j < nattrs; ++j)

  return 0;
}

//! Writes global attributes to a file by calling write_attributes().
PetscErrorCode NCGlobalAttributes::write(const PIO &nc) {
  PetscErrorCode ierr;

  ierr = write_attributes(nc, PISM_DOUBLE, false); CHKERRQ(ierr);

  return 0;
}

void NCGlobalAttributes::set_from_config(const NCConfigVariable &config) {
  this->set_string("title", config.get_string("run_title"));
  this->set_string("institution", config.get_string("institution"));
  this->set_string("command", pism_args_string());
}


//! Writes global attributes to a file. Prepends the history string.
PetscErrorCode NCGlobalAttributes::write_attributes(const PIO &nc, PISM_IO_Type, bool) const {
  int ierr;
  string old_history;

  ierr = nc.get_att_text("PISM_GLOBAL", "history", old_history); CHKERRQ(ierr);

  // Write text attributes:
  map<string, string>::const_iterator i;
  for (i = strings.begin(); i != strings.end(); ++i) {
    string name  = i->first,
      value = i->second;

    if (value.empty()) continue;

    if (name == "history") {
      // prepend:
      value = value + old_history;
    }

    ierr = nc.put_att_text("PISM_GLOBAL", name, value); CHKERRQ(ierr);
  }

  // Write double attributes:
  map<string, vector<double> >::const_iterator j;
  for (j = doubles.begin(); j != doubles.end(); ++j) {
    string name  = j->first;
    vector<double> values = j->second;

    if (values.empty()) continue;

    ierr = nc.put_att_double("PISM_GLOBAL", name, PISM_DOUBLE, values); CHKERRQ(ierr);
  }

  return 0;
}

//! Prepends `message` to the history string.
void NCGlobalAttributes::prepend_history(string message) {
  strings["history"] = message + strings["history"];
}


/// NCTimeBounds


NCTimeBounds::NCTimeBounds(PISMUnitSystem system)
  : NCVariable(system) {
  // empty
}

void NCTimeBounds::init(string var_name, string dim_name, MPI_Comm c, PetscMPIInt r) {
  NCVariable::init(var_name, c, r);
  dimension_name = dim_name;
  bounds_name    = "nv";        // number of vertices
  ndims          = 2;
}

PetscErrorCode NCTimeBounds::read(const PIO &nc, PISMTime *time,
                                  vector<double> &data) {
  PetscErrorCode ierr;
  bool variable_exists;

  // Find the variable:
  ierr = nc.inq_var(short_name, variable_exists); CHKERRQ(ierr);

  if (!variable_exists) {
    ierr = PetscPrintf(com,
		      "PISM ERROR: Can't find '%s' in '%s'.\n",
		       short_name.c_str(), nc.inq_filename().c_str());
    CHKERRQ(ierr);
    PISMEnd();
  }

  vector<string> dims;
  ierr = nc.inq_vardims(short_name, dims); CHKERRQ(ierr);

  if (dims.size() != 2) {
    ierr = PetscPrintf(com,
		       "PISM ERROR: Variable '%s' in '%s' depends on %d dimensions,\n"
		       "            but a time-bounds variable can only depend on 2 dimension.\n",
		       short_name.c_str(),
                       nc.inq_filename().c_str(), dims.size()); CHKERRQ(ierr);
    PISMEnd();
  }

  dimension_name = dims[0];
  bounds_name    = dims[1];

  unsigned int length;

  // Check that we have 2 vertices (interval end-points) per time record.
  ierr = nc.inq_dimlen(bounds_name, length); CHKERRQ(ierr);
  if (length != 2) {
    PetscPrintf(com,
                "PISM ERROR: A time-bounds variable has to have exactly 2 bounds per time record.\n"
                "            Please check that the dimension corresponding to 'number of vertices' goes\n"
                "            last in the 'ncdump -h %s' output.\n",
                nc.inq_filename().c_str());
    PISMEnd();
  }

  // Get the number of time records.
  ierr = nc.inq_dimlen(dimension_name, length); CHKERRQ(ierr);
  if (length <= 0) {
    ierr = PetscPrintf(com,
		       "PISM ERROR: Dimension %s has zero (or negative) length!\n",
		       dimension_name.c_str()); CHKERRQ(ierr);
    PISMEnd();
  }

  // Allocate memory (2 numbers per time record).
  data.resize(2*length);		// memory allocation happens here

  ierr = nc.enddef(); CHKERRQ(ierr);

  vector<unsigned int> start(2), count(2);
  start[0] = 0;
  start[1] = 0;
  count[0] = length;
  count[1] = 2;

  ierr = nc.get_vara_double(short_name, start, count, &data[0]); CHKERRQ(ierr);

  // Find the corresponding 'time' variable. (We get units from the 'time'
  // variable, because according to CF-1.5 section 7.1 a "boundary variable"
  // may not have metadata set.)
  ierr = nc.inq_var(dimension_name, variable_exists); CHKERRQ(ierr);

  if (variable_exists == false) {
    PetscPrintf(com, "PISM ERROR: Can't find '%s' in %s.\n",
                dimension_name.c_str(), nc.inq_filename().c_str());
    PISMEnd();
  }

  bool input_has_units;
  string input_units_string;
  PISMUnit input_units(m_units.get_system());

  ierr = nc.get_att_text(dimension_name, "units", input_units_string); CHKERRQ(ierr);
  input_units_string = time->CF_units_to_PISM_units(input_units_string);

  if (input_units_string.empty() == true) {
    input_has_units = false;
  } else {
    if (input_units.parse(input_units_string) != 0) {
      ierr = PetscPrintf(com,
                         "PISM ERROR: units specification '%s' is unknown or invalid (processing variable '%s').\n",
                         input_units_string.c_str(), short_name.c_str());
      PISMEnd();
    }
    input_has_units = true;
  }

  if ( has("units") && input_has_units == false ) {
    string &units_string = strings["units"];
    ierr = verbPrintf(2, com,
		      "PISM WARNING: Variable '%s' does not have the units attribute.\n"
		      "              Assuming that it is in '%s'.\n",
		      dimension_name.c_str(),
		      units_string.c_str()); CHKERRQ(ierr);
    input_units = m_units;
  }

  ierr = change_units(data, input_units, m_units); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode NCTimeBounds::write(const PIO &nc, size_t s, vector<double> &data, PISM_IO_Type nctype) {
  PetscErrorCode ierr;
  bool variable_exists = false;

  ierr = nc.inq_var(short_name, variable_exists); CHKERRQ(ierr);
  if (!variable_exists) {
    ierr = define(nc, nctype, true); CHKERRQ(ierr);
  }

  // convert to glaciological units:
  ierr = change_units(data, m_units, m_glaciological_units); CHKERRQ(ierr);

  ierr = nc.enddef(); CHKERRQ(ierr);

  vector<unsigned int> start(2), count(2);
  start[0] = static_cast<unsigned int>(s);
  start[1] = 0;
  count[0] = static_cast<unsigned int>(data.size()) / 2;
  count[1] = 2;

  ierr = nc.put_vara_double(short_name, start, count, &data[0]); CHKERRQ(ierr);

  // restore internal units:
  ierr = change_units(data, m_glaciological_units, m_units); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode NCTimeBounds::write(const PIO &nc, size_t start, double a, double b, PISM_IO_Type nctype) {
  vector<double> tmp(2);
  tmp[0] = a;
  tmp[1] = b;
  PetscErrorCode ierr = write(nc, start, tmp, nctype); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode NCTimeBounds::change_units(vector<double> &data, PISMUnit &from, PISMUnit &to) {
  PetscErrorCode ierr;
  string from_name, to_name;

  from_name = from.format();
  to_name   = to.format();

  // Get the converter:
  cv_converter *c = to.get_converter_from(from);
  if (c == NULL) {              // can't convert
    ierr = PetscPrintf(com,
                       "PISM ERROR: NCTimeBounds '%s': attempted to convert data from '%s' to '%s'.\n",
                       short_name.c_str(), from_name.c_str(), to_name.c_str());
    PISMEnd();
  }

  cv_convert_doubles(c, &data[0], data.size(), &data[0]);
  cv_free(c);

  return 0;
}


PetscErrorCode NCTimeBounds::define(const PIO &nc, PISM_IO_Type nctype, bool) {
  PetscErrorCode ierr;
  vector<string> dims;
  bool exists;

  ierr = nc.inq_var(short_name, exists); CHKERRQ(ierr);
  if (exists)
    return 0;

  ierr = nc.redef(); CHKERRQ(ierr);

  ierr = nc.inq_dim(dimension_name, exists); CHKERRQ(ierr);
  if (exists == false) {
    map<string,string> tmp;
    ierr = nc.def_dim(dimension_name, PISM_UNLIMITED, tmp); CHKERRQ(ierr);
  }

  ierr = nc.inq_dim(bounds_name, exists); CHKERRQ(ierr);
  if (exists == false) {
    map<string,string> tmp;
    ierr = nc.def_dim(bounds_name, 2, tmp); CHKERRQ(ierr);
  }

  dims.push_back(dimension_name);
  dims.push_back(bounds_name);

  ierr = nc.redef(); CHKERRQ(ierr);

  ierr = nc.def_var(short_name, nctype, dims); CHKERRQ(ierr);

  ierr = write_attributes(nc, nctype, true);

  return 0;
}
