// Copyright (C) 2008--2010 Ed Bueler and Constantine Khroulev
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

#include "pism_const.hh"
#include "iceModelVec.hh"


IceModelVec::IceModelVec() {

  shallow_copy = false;
  v = PETSC_NULL;
  da = PETSC_NULL;
  grid = PETSC_NULL;
  array = PETSC_NULL;
  localp = true;
  use_interpolation_mask = false;

  dims = GRID_2D;		// default
  dof = 1;			// default
  s_width = 1;

  access_counter = 0;

  name = "unintialized variable";

  vars.resize(dof);

  map_viewers = new map<string,PetscViewer>;
  reset_attrs();
}


//! Creates a shallow copy of an \c IceModelVec.
/*!
  No data is copied to the new IceModelVec.

  The difference is that such a copy will not free the memory when deleted (or
  goes out of scope).
 */
IceModelVec::IceModelVec(const IceModelVec &other) {
  // This IceModelVec is a shallow copy!
  shallow_copy = true;

  v = other.v;
  da = other.da;
  dims = other.dims;
  dof = other.dof;
  grid = other.grid;
  array = other.array;
  access_counter = other.access_counter;
  localp = other.localp;

  map_viewers = other.map_viewers;

  use_interpolation_mask = other.use_interpolation_mask;
  interpolation_mask = other.interpolation_mask;

  name = other.name;

  vars = other.vars;
}


IceModelVec::~IceModelVec() {
  // Only destroy the IceModelVec if it is not a shallow copy:
  if (!shallow_copy) destroy();
}

//! Returns true if create() was called and false otherwise.
bool IceModelVec::was_created() {
  return (v != PETSC_NULL);
}

//! Returns the grid type of an IceModelVec. (This is the way to figure out if an IceModelVec is 2D or 3D).
GridType IceModelVec::grid_type() {
  return dims;
}

PetscErrorCode  IceModelVec::destroy() {
  PetscErrorCode ierr;

  if (v != PETSC_NULL) {
    ierr = VecDestroy(v); CHKERRQ(ierr);
    v = PETSC_NULL;
  }
  if (da != PETSC_NULL) {
    ierr = DADestroy(da); CHKERRQ(ierr);
    da = PETSC_NULL;
  }

  // map-plane viewers:
  if (map_viewers != NULL) {
    map<string,PetscViewer>::iterator i;
    for (i = (*map_viewers).begin(); i != (*map_viewers).end(); ++i) {
      if ((*i).second != PETSC_NULL) {
	ierr = PetscViewerDestroy((*i).second); CHKERRQ(ierr);
      }
    }
    delete map_viewers;
    map_viewers = NULL;
  }

  return 0;
}

//! Result: min <- min(v[j]), max <- max(v[j]).
/*! 
PETSc manual correctly says "VecMin and VecMax are collective on Vec" but
GlobalMax,GlobalMin \e are needed, when localp==true, to get correct 
values because Vecs created with DACreateLocalVector() are of type 
VECSEQ and not VECMPI.  See src/trypetsc/localVecMax.c.
 */
PetscErrorCode IceModelVec::range(PetscReal &min, PetscReal &max) {
  PetscReal my_min, my_max, gmin, gmax;
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);

  ierr = VecMin(v, PETSC_NULL, &my_min); CHKERRQ(ierr);
  ierr = VecMax(v, PETSC_NULL, &my_max); CHKERRQ(ierr);

  if (localp) {
    // needs a reduce operation; use PetscGlobalMax;
    ierr = PetscGlobalMin(&my_min, &gmin, grid->com); CHKERRQ(ierr);
    ierr = PetscGlobalMax(&my_max, &gmax, grid->com); CHKERRQ(ierr);
    min = gmin;
    max = gmax;
  } else {
    min = my_min;
    max = my_max;
  }
  return 0;
}


//! Computes the norm of an IceModelVec by calling PETSc VecNorm.
/*! 
See comment for range(); because local Vecs are VECSEQ, needs a reduce operation.
See src/trypetsc/localVecMax.c.
 */
PetscErrorCode IceModelVec::norm(NormType n, PetscReal &out) {
  PetscErrorCode ierr;
  PetscReal my_norm, gnorm;
  ierr = checkAllocated(); CHKERRQ(ierr);

  ierr = VecNorm(v, n, &my_norm); CHKERRQ(ierr);

  if (localp) {
    // needs a reduce operation; use PetscGlobalMax if NORM_INFINITY,
    //   otherwise PetscGlobalSum; carefully in NORM_2 case
    if (n == NORM_1_AND_2) {
      SETERRQ1(1, 
         "IceModelVec::norm(...): NORM_1_AND_2 not implemented (called as %s.norm(...))\n",
         name.c_str());
    } else if (n == NORM_1) {
      ierr = PetscGlobalSum(&my_norm, &gnorm, grid->com); CHKERRQ(ierr);
    } else if (n == NORM_2) {
      my_norm = PetscSqr(my_norm);  // undo sqrt in VecNorm before sum
      ierr = PetscGlobalSum(&my_norm, &gnorm, grid->com); CHKERRQ(ierr);
      gnorm = sqrt(gnorm);
    } else if (n == NORM_INFINITY) {
      ierr = PetscGlobalMax(&my_norm, &gnorm, grid->com); CHKERRQ(ierr);
    } else {
      SETERRQ1(2, "IceModelVec::norm(...): unknown NormType (called as %s.norm(...))\n",
         name.c_str());
    }
    out = gnorm;
  } else {
    out = my_norm;
  }
  return 0;
}


//! Result: v <- sqrt(v), elementwise.  Calls VecSqrt(v).
/*!
Name avoids clash with sqrt() in math.h.
 */
PetscErrorCode IceModelVec::squareroot() {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);

  ierr = VecSqrt(v); CHKERRQ(ierr);
  return 0;
}


//! Result: v <- v + alpha * x. Calls VecAXPY.
PetscErrorCode IceModelVec::add(PetscScalar alpha, IceModelVec &x) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = x.checkAllocated(); CHKERRQ(ierr);
  ierr = checkCompatibility("add", x); CHKERRQ(ierr);

  ierr = VecAXPY(v, alpha, x.v); CHKERRQ(ierr);
  return 0;
}

//! Result: result <- v + alpha * x. Calls VecWAXPY.
PetscErrorCode IceModelVec::add(PetscScalar alpha, IceModelVec &x, IceModelVec &result) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = x.checkAllocated(); CHKERRQ(ierr);
  ierr = result.checkAllocated(); CHKERRQ(ierr);
  ierr = checkCompatibility("add", x); CHKERRQ(ierr);
  ierr = checkCompatibility("add", result); CHKERRQ(ierr);
  
  ierr = VecWAXPY(result.v, alpha, x.v, v); CHKERRQ(ierr);
  return 0;
}

//! Result: v[j] <- v[j] + alpha for all j. Calls VecShift.
PetscErrorCode IceModelVec::shift(PetscScalar alpha) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);

  ierr = VecShift(v, alpha); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- v * alpha. Calls VecScale.
PetscErrorCode IceModelVec::scale(PetscScalar alpha) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);

  ierr = VecScale(v, alpha); CHKERRQ(ierr);
  return 0;
}

//! Result: result <- v .* x.  Calls VecPointwiseMult.
PetscErrorCode  IceModelVec::multiply_by(IceModelVec &x, IceModelVec &result) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = x.checkAllocated(); CHKERRQ(ierr);
  ierr = checkCompatibility("multiply_by", x); CHKERRQ(ierr);
  ierr = checkCompatibility("multiply_by", result); CHKERRQ(ierr);

  ierr = VecPointwiseMult(result.v, v, x.v); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- v .* x.  Calls VecPointwiseMult.
PetscErrorCode  IceModelVec::multiply_by(IceModelVec &x) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = x.checkAllocated(); CHKERRQ(ierr);
  ierr = checkCompatibility("multiply_by", x); CHKERRQ(ierr);

  ierr = VecPointwiseMult(v, v, x.v); CHKERRQ(ierr);
  return 0;
}

//! Copies v to a global vector 'destination'. Ghost points are discarded.
/*! This is potentially dangerous: make sure that \c destination has the same
    dimensions as the current IceModelVec.
 */
PetscErrorCode  IceModelVec::copy_to(Vec destination) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  if (localp) {
    ierr = DALocalToGlobal(da, v, INSERT_VALUES, destination); CHKERRQ(ierr);
  } else {
    ierr = VecCopy(v, destination); CHKERRQ(ierr);
  }
  return 0;
}

PetscErrorCode IceModelVec::copy_from(Vec source) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  if (localp) {
    ierr =   DAGlobalToLocalBegin(da, source, INSERT_VALUES, v);  CHKERRQ(ierr);
    ierr =     DAGlobalToLocalEnd(da, source, INSERT_VALUES, v);  CHKERRQ(ierr);
  } else {
    ierr = VecCopy(source, v); CHKERRQ(ierr);
  }
  return 0;
}


//! Result: destination <- v.  Leaves metadata alone but copies values in Vec.  Uses VecCopy.
PetscErrorCode  IceModelVec::copy_to(IceModelVec &destination) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = destination.checkAllocated(); CHKERRQ(ierr);
  ierr = checkCompatibility("copy_to", destination); CHKERRQ(ierr);

  ierr = VecCopy(v, destination.v); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- source.  Leaves metadata alone but copies values in Vec.  Uses VecCopy.
PetscErrorCode  IceModelVec::copy_from(IceModelVec &source) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = source.checkAllocated(); CHKERRQ(ierr);
  ierr = checkCompatibility("copy_from", source); CHKERRQ(ierr);

  ierr = VecCopy(source.v, v); CHKERRQ(ierr);
  return 0;
}

//! Sets the variable name to \c name.
PetscErrorCode  IceModelVec::set_name(const char new_name[], int N) {
  reset_attrs();
  
  if (N == 0)
    name = new_name;

  vars[N].short_name = new_name;

  return 0;
}

//! Sets the glaciological units of an IceModelVec.
/*!
This affects NCVariable::report_range() and IceModelVec::write().  In write(),
if IceModelVec::write_in_glaciological_units == true, then that variable is written
with a conversion to the glaciological units set here.
 */
PetscErrorCode  IceModelVec::set_glaciological_units(string my_units) {

  PetscErrorCode ierr;
    
  for (int j = 0; j < dof; ++j) {
   ierr = vars[j].set_glaciological_units(my_units); CHKERRQ(ierr);
  }
  
  return 0;
}

//! Resets most IceModelVec attributes.
PetscErrorCode IceModelVec::reset_attrs() {

  time_independent = false;
  write_in_glaciological_units = false;
  output_data_type = NC_DOUBLE;

  for (int j = 0; j < dof; ++j) {
    vars[j].reset();
    // _FillValue should be a number and should depend on the quantity in
    //     question. It should also be outside the valid range.
    //     vars[j].set("_FillValue", GSL_NAN);
  }

  return 0;
}

//! Sets NetCDF attributes of an IceModelVec object.
/*! Call set_attrs("new long name", "new units", "new pism_intent", "") if a
  variable does not have a standard name. Similarly, by putting "" in an
  appropriate spot, it is possible tp leave long_name, units or pism_intent
  unmodified.

  If my_units != "", this also resets glaciological_units, so that they match
  internal units.
 */
PetscErrorCode IceModelVec::set_attrs(string my_pism_intent,
				      string my_long_name,
				      string my_units,
				      string my_standard_name,
				      int N) {

  if (!my_long_name.empty()) {
    vars[N].set_string("long_name", my_long_name);
  }

  if (!my_units.empty()) {
    PetscErrorCode ierr = vars[N].set_units(my_units); CHKERRQ(ierr);
  }

  if (!my_pism_intent.empty()) {
    vars[N].set_string("pism_intent", my_pism_intent);
  }

  if (!my_standard_name.empty()) {
    vars[N].set_string("standard_name", my_standard_name);
  }

  return 0;
}

//! Gets an IceModelVec from a file \c filename, interpolating onto the current grid.
/*! Stops if the variable was not found and \c critical == true.
 */
PetscErrorCode IceModelVec::regrid(const char filename[], LocalInterpCtx &lic, 
                                   bool critical) {
  PetscErrorCode ierr;
  MaskInterp *m = NULL;
  Vec g;

  if (dof != 1)
    SETERRQ(1, "This method only supports IceModelVecs with dof == 1.");

  if (use_interpolation_mask) m = &interpolation_mask;

  if (localp) {
    ierr = DACreateGlobalVector(da, &g); CHKERRQ(ierr);

    ierr = vars[0].regrid(filename, lic, critical, false, 0.0, m, g); CHKERRQ(ierr);

    ierr = DAGlobalToLocalBegin(da, g, INSERT_VALUES, v); CHKERRQ(ierr);
    ierr = DAGlobalToLocalEnd(da, g, INSERT_VALUES, v); CHKERRQ(ierr);

    ierr = VecDestroy(g); CHKERRQ(ierr);
  } else {
    ierr = vars[0].regrid(filename, lic, critical, false, 0.0, m, v); CHKERRQ(ierr);
  }

  return 0;
}

//! Gets an IceModelVec from a file \c filename, interpolating onto the current grid.
/*! Sets all the values to \c default_value if the variable was not found.
 */
PetscErrorCode IceModelVec::regrid(const char filename[], 
                                   LocalInterpCtx &lic, PetscScalar default_value) {
  PetscErrorCode ierr;
  MaskInterp *m = NULL;
  Vec g;

  if (dof != 1)
    SETERRQ(1, "This method only supports IceModelVecs with dof == 1.");

  if (use_interpolation_mask) m = &interpolation_mask;

  if (localp) {
    ierr = DACreateGlobalVector(da, &g); CHKERRQ(ierr);

    ierr = vars[0].regrid(filename, lic, false, true, default_value, m, g); CHKERRQ(ierr);

    ierr = DAGlobalToLocalBegin(da, g, INSERT_VALUES, v); CHKERRQ(ierr);
    ierr = DAGlobalToLocalEnd(da, g, INSERT_VALUES, v); CHKERRQ(ierr);

    ierr = VecDestroy(g); CHKERRQ(ierr);
  } else {
    ierr = vars[0].regrid(filename, lic, false, true, default_value, m, v); CHKERRQ(ierr);
  }

  return 0;
}

//! Reads appropriate NetCDF variable(s) into an IceModelVec.
PetscErrorCode IceModelVec::read(const char filename[], const unsigned int time) {           
  PetscErrorCode ierr;
  Vec g;

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(grid->com, "  Reading %s...\n", name.c_str()); CHKERRQ(ierr);
  }

  if (dof != 1)
    SETERRQ(1, "This method only supports IceModelVecs with dof == 1.");

  if (localp) {
    ierr = DACreateGlobalVector(da, &g); CHKERRQ(ierr);

    ierr = vars[0].read(filename, time, g); CHKERRQ(ierr);

    ierr = DAGlobalToLocalBegin(da, g, INSERT_VALUES, v); CHKERRQ(ierr);
    ierr = DAGlobalToLocalEnd(da, g, INSERT_VALUES, v); CHKERRQ(ierr);

    ierr = VecDestroy(g); CHKERRQ(ierr);
  } else {
    ierr = vars[0].read(filename, time, v); CHKERRQ(ierr);
  }

  return 0;
}

//! Writes an IceModelVec to a NetCDF file using the default output data type.
PetscErrorCode IceModelVec::write(const char filename[]) {
  PetscErrorCode ierr;

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(grid->com, "  Writing %s...\n", name.c_str()); CHKERRQ(ierr);
  }
  
  ierr = write(filename, output_data_type); CHKERRQ(ierr);

  return 0;
}

//! Writes an IceModelVec to a NetCDF file.
PetscErrorCode IceModelVec::write(const char filename[], nc_type nctype) {
  PetscErrorCode ierr;
  Vec g;

  if (dof != 1)
    SETERRQ(1, "This method only supports IceModelVecs with dof == 1.");

  vars[0].time_independent = time_independent;

  if (localp) {
    ierr = DACreateGlobalVector(da, &g); CHKERRQ(ierr);
    ierr = DALocalToGlobal(da, v, INSERT_VALUES, g); CHKERRQ(ierr);

    ierr = vars[0].write(filename, nctype, write_in_glaciological_units, g); CHKERRQ(ierr);

    ierr = VecDestroy(g); CHKERRQ(ierr);
  } else {
    ierr = vars[0].write(filename, nctype, write_in_glaciological_units, v); CHKERRQ(ierr);
  }

  return 0;
}

//! Checks if an IceModelVec is allocated.  Terminates if not.
PetscErrorCode  IceModelVec::checkAllocated() {
  if (v == PETSC_NULL) {
    SETERRQ1(1,"IceModelVec ERROR: IceModelVec with name='%s' WAS NOT allocated\n",
             name.c_str());
  }
  return 0;
}

//! Checks if the access to the array is available.
PetscErrorCode  IceModelVec::checkHaveArray() {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  if (array == PETSC_NULL) {
    SETERRQ1(1,"array for IceModelVec with name='%s' not available\n"
               "  (REMEMBER TO RUN begin_access() before access and end_access() after access)\n",
               name.c_str());
  }
  return 0;
}

//! Checks if two IceModelVecs have compatible sizes, dimensions and numbers of degrees of freedom.
PetscErrorCode IceModelVec::checkCompatibility(const char* func, IceModelVec &other) {
  PetscErrorCode ierr;
  PetscInt X_size, Y_size;

  if (dof != other.dof) {
    SETERRQ1(1, "IceModelVec::%s(...): operands have different numbers of degrees of freedom",
	     func);
  }

  if (dims != other.dims) {
    SETERRQ1(1, "IceModelVec::%s(...): operands have different numbers of dimensions",
	     func);
  }

  ierr = VecGetSize(v, &X_size); CHKERRQ(ierr);
  ierr = VecGetSize(other.v, &Y_size); CHKERRQ(ierr);
  if (X_size != Y_size)
    SETERRQ3(1, "IceModelVec::%s(...): incompatible Vec sizes (called as %s.%s(...))\n",
	     func, name.c_str(), func);


  return 0;
}

//! Checks if an IceModelVec is allocated and calls DAVecGetArray.
PetscErrorCode  IceModelVec::begin_access() {
  PetscErrorCode ierr;
#ifdef PISM_DEBUG
  ierr = checkAllocated(); CHKERRQ(ierr);

  if (access_counter < 0)
    SETERRQ(1, "IceModelVec::begin_access(): access_counter < 0");
#endif

  if (access_counter == 0) {
    ierr = DAVecGetArray(da, v, &array); CHKERRQ(ierr);
  }

  access_counter++;

  return 0;
}

//! Checks if an IceModelVec is allocated and calls DAVecRestoreArray.
PetscErrorCode  IceModelVec::end_access() {
  PetscErrorCode ierr;
  access_counter--;

#ifdef PISM_DEBUG
  ierr = checkAllocated(); CHKERRQ(ierr);

  if (access_counter < 0)
    SETERRQ(1, "IceModelVec::end_access(): access_counter < 0");
#endif

  if (access_counter == 0) {
    ierr = DAVecRestoreArray(da, v, &array); CHKERRQ(ierr);
    array = NULL;
  }

  return 0;
}

//! Starts the communication of ghost points.
PetscErrorCode  IceModelVec::beginGhostComm() {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has name='%s')\n",
               name.c_str());
  }
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalBegin(da, v, INSERT_VALUES, v);  CHKERRQ(ierr);
  return 0;
}

//! Ends the communication of ghost points.
PetscErrorCode  IceModelVec::endGhostComm() {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has name='%s')\n",
               name.c_str());
  }
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(da, v, INSERT_VALUES, v); CHKERRQ(ierr);
  return 0;
}

//! Starts the communication of ghost points to IceModelVec destination.
PetscErrorCode  IceModelVec::beginGhostComm(IceModelVec &destination) {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has name='%s')",
               name.c_str());
  }

  if (!destination.localp) {
    SETERRQ(1, "IceModelVec::beginGhostComm(): destination has to be local.");
  }

  if (dims != destination.dims) {
    SETERRQ(1, "IceModelVec::beginGhostComm(): operands have different numbers of dimensions.");
  }

  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalBegin(da, v, INSERT_VALUES, destination.v);  CHKERRQ(ierr);
  return 0;
}

//! Ends the communication of ghost points to IceModelVec destination.
PetscErrorCode  IceModelVec::endGhostComm(IceModelVec &destination) {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has name='%s')\n",
               name.c_str());
  }

  if (!destination.localp) {
    SETERRQ(1, "IceModelVec::beginGhostComm(): destination has to be local.");
  }

  if (dims != destination.dims) {
    SETERRQ(1, "IceModelVec::beginGhostComm(): operands have different numbers of dimensions.");
  }

  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(da, v, INSERT_VALUES, destination.v); CHKERRQ(ierr);
  return 0;
}

//! Result: v[j] <- c for all j.
PetscErrorCode  IceModelVec::set(const PetscScalar c) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = VecSet(v,c); CHKERRQ(ierr);
  return 0;
}

//! Checks if a value \c a in in the range of valid values of an IceModelVec.
/*!
  uses valid_min and valid_max attributes, which can be set using the set_attr() method.
 */
bool IceModelVec::is_valid(PetscScalar a, int N) {
  return vars[N].is_valid(a);
}

//! Set a string attribute of an IceModelVec.
/*! Attributes "units" and "glaciological_units" are also parsed to be used for unit conversion.
 */
PetscErrorCode IceModelVec::set_attr(string attr, string value, int N) {
  PetscErrorCode ierr;

  if (attr == "units") {
    ierr = vars[N].set_units(value); CHKERRQ(ierr);
    return 0;
  }

  if (attr == "glaciological_units") {
    ierr = vars[N].set_glaciological_units(value); CHKERRQ(ierr);
    return 0;
  }

  vars[N].set_string(attr, value);
  return 0;
}

//! Sets a single-valued double attribute.
PetscErrorCode IceModelVec::set_attr(string my_name, double value, int N) {
  vars[N].set(my_name, value);
  return 0;
}

//! Sets an array attribute.
PetscErrorCode IceModelVec::set_attr(string my_name, vector<double> values,
				     int N) {
  vars[N].doubles[my_name] = values;
  return 0;
}

bool IceModelVec::has_attr(string my_name, int N) {
  return vars[N].has(my_name);
}

//! Returns a single-valued double attribute.
double IceModelVec::double_attr(string my_name, int N) {
  return vars[N].get(my_name);
}

//! Returns a string attribute.
string IceModelVec::string_attr(string n, int N) {

  if (n == "name")
    return name;

  return vars[N].get_string(n);
}

//! Returns an array attribute.
vector<double> IceModelVec::array_attr(string my_name, int N) {
  return vars[N].doubles[my_name];
}

//! Creates a run-time diagnostic viewer.
PetscErrorCode IceModelVec::create_viewer(PetscInt viewer_size, string title, PetscViewer &viewer) {
  PetscErrorCode ierr;
  int x, y;

  ierr = compute_viewer_size(viewer_size, x, y); CHKERRQ(ierr);

  // note we reverse x <-> y; see IceGrid::createDA() for original reversal
  ierr = PetscViewerDrawOpen(grid->com, PETSC_NULL, title.c_str(),
			     PETSC_DECIDE, PETSC_DECIDE, y, x, &viewer);  CHKERRQ(ierr);

  // following should be redundant, but may put up a title even under 2.3.3-p1:3 where
  // there is a no titles bug
  PetscDraw draw;
  ierr = PetscViewerDrawGetDraw(viewer, 0, &draw); CHKERRQ(ierr);
  ierr = PetscDrawSetTitle(draw, title.c_str()); CHKERRQ(ierr);

  return 0;
}

//! Computes the size of a diagnostic viewer window.
PetscErrorCode IceModelVec::compute_viewer_size(int target_size, int &x, int &y) {
  PetscScalar Lx = grid->Lx, Ly = grid->Ly;

  // aim for smaller dimension equal to target, larger dimension larger by Ly/Lx or Lx/Ly proportion
  const double  yTOx = Ly / Lx;
  if (Ly > Lx) {
    x = target_size; 
    y = (PetscInt) ((double)target_size * yTOx); 
  } else {
    y = target_size; 
    x = (PetscInt) ((double)target_size / yTOx);
  }
  
  // if either dimension is larger than twice the target, shrink appropriately
  if (x > 2 * target_size) {
    y = (PetscInt) ( (double)(y) * (2.0 * (double)target_size / (double)(x)) );
    x = 2 * target_size;
  } else if (y > 2 * target_size) {
    x = (PetscInt) ( (double)(x) * (2.0 * (double)target_size / (double)(y)) );
    y = 2 * target_size;
  }
  
  // make sure minimum dimension is sufficient to see
  if (x < 20)   x = 20;
  if (y < 20)   y = 20;
  return 0;
}

//! Checks if the current IceModelVec has NANs and stops if it does.
PetscErrorCode IceModelVec::has_nan() {
  PetscErrorCode ierr;
  PetscReal tmp;

  ierr = norm(NORM_INFINITY, tmp); CHKERRQ(ierr);

  if ( gsl_isnan(tmp) ) {
    //    SETERRQ1(1, "IceModelVec %s has NANs", name.c_str());
    PetscPrintf(grid->com, "IceModelVec %s has uninitialized grid points (or NANs)\n", name.c_str());
  }

  return 0;
}

/********* IceModelVec3 and IceModelVec3Bedrock: SEE SEPARATE FILE  iceModelVec3.cc    **********/

/********* IceModelVec2: SEE SEPARATE FILE  iceModelVec2.cc    **********/
