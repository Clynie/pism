// Copyright (C) 2008, 2009, 2010 Ed Bueler and Constantine Khroulev
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

#include <cstring>
#include <cstdlib>
#include <petscda.h>
#include "../udunits/udunits.h"
#include "nc_util.hh"

#ifndef __IceModelVec_hh
#define __IceModelVec_hh

//! Abstract class for reading, writing, allocating, and accessing a DA-based PETSc Vec from within IceModel.
class IceModelVec {
public:
  IceModelVec();
  virtual ~IceModelVec();

  virtual PetscErrorCode  create(IceGrid &mygrid, const char my_short_name[], bool local);
  virtual bool            was_created();
  virtual PetscErrorCode  destroy();

  virtual PetscErrorCode  printInfo(const PetscInt verbosity);
  virtual PetscErrorCode  range(PetscReal &min, PetscReal &max);
  virtual PetscErrorCode  norm(NormType n, PetscReal &out);
  virtual PetscErrorCode  add(PetscScalar alpha, IceModelVec &x);
  virtual PetscErrorCode  add(PetscScalar alpha, IceModelVec &x, IceModelVec &result);
  virtual PetscErrorCode  squareroot();
  virtual PetscErrorCode  shift(PetscScalar alpha);
  virtual PetscErrorCode  scale(PetscScalar alpha);
  virtual PetscErrorCode  multiply_by(IceModelVec &x, IceModelVec &result);
  virtual PetscErrorCode  multiply_by(IceModelVec &x);
  virtual PetscErrorCode  copy_to_global(Vec destination);
  virtual PetscErrorCode  copy_to(IceModelVec &destination);
  virtual PetscErrorCode  copy_from(IceModelVec &source);
  virtual PetscErrorCode  set_name(const char name[]);
  virtual PetscErrorCode  set_glaciological_units(const char units[]);
  virtual PetscErrorCode  set_attrs(const char my_pism_intent[], const char my_long_name[],
				    const char my_units[], const char my_standard_name[]);
  virtual PetscErrorCode  set_valid_range(PetscReal min, PetscReal max);
  virtual PetscErrorCode  set_valid_min(PetscReal min);
  virtual PetscErrorCode  set_valid_max(PetscReal min);
  virtual bool            is_valid(PetscScalar a);
  virtual PetscErrorCode  set_coordinates(const char name[]);
  virtual PetscErrorCode  write_attrs(const int ncid, nc_type nctype);
  virtual PetscErrorCode  write_text_attr(const char filename[], const char name[], 
                                          const char *tp);
  virtual PetscErrorCode  write_scalar_attr(const char filename[], const char name[],
                                            nc_type nctype, size_t len, const double *dp);
  virtual PetscErrorCode  write(const char filename[], nc_type nctype);
  virtual PetscErrorCode  read(const char filename[], const unsigned int time);
  virtual PetscErrorCode  put_on_proc0(Vec onp0, VecScatter ctx, Vec g2, Vec g2natural);
  virtual PetscErrorCode  get_from_proc0(Vec onp0, VecScatter ctx, Vec g2, Vec g2natural);
  virtual PetscErrorCode  regrid(const char filename[], LocalInterpCtx &lic, bool critical);
  virtual PetscErrorCode  regrid(const char filename[], LocalInterpCtx &lic, PetscScalar default_value);
  virtual PetscErrorCode  report_range();

  virtual PetscErrorCode  begin_access();
  virtual PetscErrorCode  end_access();
  virtual PetscErrorCode  beginGhostComm();
  virtual PetscErrorCode  endGhostComm();
  virtual PetscErrorCode  beginGhostComm(IceModelVec &destination);
  virtual PetscErrorCode  endGhostComm(IceModelVec &destination);


  virtual PetscErrorCode  set(const PetscScalar c);
 
  MaskInterp interpolation_mask;
  bool   use_interpolation_mask, write_in_glaciological_units;
protected:
#ifdef PISM_DEBUG
  int creation_counter, access_counter;
#endif // PISM_DEBUG

  Vec  v;
  char short_name[PETSC_MAX_PATH_LEN],    // usually the name of the NetCDF
					  // variable (unless there is no
					  // corresponding NetCDF var)
    long_name[PETSC_MAX_PATH_LEN],     // NetCDF attribute
    standard_name[PETSC_MAX_PATH_LEN], // NetCDF attribute; sometimes specified in CF convention
    units_string[PETSC_MAX_PATH_LEN],  // NetCDF attribute
    glaciological_units_string[PETSC_MAX_PATH_LEN], // a human-friendly units string
    coordinates[PETSC_MAX_PATH_LEN],		    // a list of coordinates, usually "lat lon"
    pism_intent[PETSC_MAX_PATH_LEN];	  // NetCDF attribute

  // An IceModelVec always has a short_name, but it may not have the following attributes:
  bool has_long_name, has_units, has_pism_intent, has_standard_name,
    has_valid_min, has_valid_max, has_coordinates;

  PetscReal valid_min, valid_max;

  utUnit units,		      //< internal (PISM) units
         glaciological_units; //< for diagnostic variables: units to use when writing
			      //< to a NetCDF file and for standard out reports

  IceGrid      *grid;
  GridType     dims;
  DA           da;
  bool         localp, IOwnDA;

  void         *array;  // will be PetscScalar** or PetscScalar*** in derived classes

  virtual PetscErrorCode checkAllocated();
  virtual PetscErrorCode checkHaveArray();
  virtual PetscErrorCode check_range(const int ncid, const int varid);
  virtual PetscErrorCode define_netcdf_variable(int ncid, nc_type nctype, int *varidp); // virtual only
  virtual PetscErrorCode regrid_from_netcdf(const char filename[],
					    LocalInterpCtx &lic, bool critical,
					    bool set_default_value,
					    PetscScalar default_value);
  virtual PetscErrorCode read_valid_range(const int ncid, const int varid);
  virtual PetscErrorCode change_units(utUnit *from, utUnit *to);
  virtual PetscErrorCode reset_attrs();
  // FIXME: consider adding 
  //   virtual PetscErrorCode  checkSelfOwnsIt(const PetscInt i, const PetscInt j);
  //   virtual PetscErrorCode  checkSelfOwnsItGhosted(const PetscInt i, const PetscInt j);
};


struct planeStar {
  PetscScalar ij, ip1, im1, jp1, jm1;
};

struct planeBox {
  PetscScalar ij, ip1, im1, jp1, jm1, ip1jp1, im1jp1, ip1jm1, im1jm1;
};


//! Class for a 2d DA-based Vec for ice scalar quantities in IceModel.
class IceModelVec2 : public IceModelVec {
public:
  IceModelVec2();
  virtual PetscErrorCode  create(IceGrid &my_grid, const char my_short_name[], bool local);
  PetscErrorCode  create(IceGrid &my_grid, const char my_short_name[], bool local,
			 DAStencilType my_sten, int stencil_width);
  virtual PetscErrorCode  createSameDA(IceModelVec2 imv2_source,
				       IceGrid &my_grid, const char my_short_name[], bool local);
  PetscErrorCode  get_array(PetscScalar** &a);

protected:
  virtual PetscErrorCode  define_netcdf_variable(int ncid, nc_type nctype, int *varidp);
};


//! Class for a 3d DA-based Vec for bedrock (lithosphere) scalar quantities in IceModel.
class IceModelVec3Bedrock : public IceModelVec {
public:
  IceModelVec3Bedrock();
  virtual PetscErrorCode create(IceGrid &mygrid, const char my_short_name[], bool local);

  PetscErrorCode  setInternalColumn(const PetscInt i, const PetscInt j, PetscScalar *valsIN);
  PetscErrorCode  setColumn(const PetscInt i, const PetscInt j,
                                      const PetscScalar c);
  PetscErrorCode  getInternalColumn(const PetscInt i, const PetscInt j, PetscScalar **valsOUT);

  PetscErrorCode  setValColumn(const PetscInt i, const PetscInt j, const PetscInt nlevels, 
                               PetscScalar *levelsIN, PetscScalar *valsIN);
  PetscErrorCode  getValColumn(const PetscInt i, const PetscInt j, const PetscInt nlevels, 
                               PetscScalar *levelsIN, PetscScalar *valsOUT);

protected:  
  PetscErrorCode  isLegalLevel(const PetscScalar z);
  virtual PetscErrorCode  define_netcdf_variable(int ncid, nc_type nctype, int *varidp);
};


//! Class for a 3d DA-based Vec for ice scalar quantities in IceModel.
class IceModelVec3 : public IceModelVec {
public:
  IceModelVec3();
  virtual PetscErrorCode  create(IceGrid &mygrid, const char my_short_name[], bool local);
  PetscErrorCode          createSameDA(IceModelVec3 imv3_dasource, 
                                       IceGrid &mygrid, const char my_short_name[], bool local);

  // note the IceModelVec3 with this method must be *local* while imv3_source must be *global*
  virtual PetscErrorCode  beginGhostCommTransfer(IceModelVec3 imv3_source);
  virtual PetscErrorCode  endGhostCommTransfer(IceModelVec3 imv3_source);

  // need call begin_access() before set...() or get...() *and* need call end_access() afterward
  PetscErrorCode  setValColumnPL(const PetscInt i, const PetscInt j, const PetscInt nlevels, 
                               PetscScalar *levelsIN, PetscScalar *valsIN);
  PetscErrorCode  setColumn(const PetscInt i, const PetscInt j, const PetscScalar c);
  PetscErrorCode  setInternalColumn(const PetscInt i, const PetscInt j, PetscScalar *valsIN);

  PetscScalar     getValZ(const PetscInt i, const PetscInt j, const PetscScalar z);

  PetscErrorCode  getPlaneStarZ(const PetscInt i, const PetscInt j, const PetscScalar z,
                                planeStar *star);

//Idea; see IceModel::horizontalVelocitySIARegular(), for example; would only work for 4 neighbors
//   if used with two IceModelVec3:
//  PetscErrorCode  averageStagNeighCol(const PetscInt i, const PetscInt j, 
//                                      const PetscInt nlevels, PetscScalar *levelsIN, 
//                                      PetscScalar *avOUT);

  PetscErrorCode  getValColumnPL(const PetscInt i, const PetscInt j, const PetscInt nlevels, 
                                 PetscScalar *levelsIN, PetscScalar *valsOUT);
  PetscErrorCode  getValColumnQUAD(const PetscInt i, const PetscInt j, const PetscInt nlevels, 
                                   PetscScalar *levelsIN, PetscScalar *valsOUT);
  PetscErrorCode  getInternalColumn(const PetscInt i, const PetscInt j, PetscScalar **valsPTR);

  PetscErrorCode  getHorSlice(Vec &gslice, const PetscScalar z); // used in iMmatlab.cc
  PetscErrorCode  getHorSlice(IceModelVec2 gslice, const PetscScalar z);
  PetscErrorCode  getSurfaceValues(Vec &gsurf, IceModelVec2 myH); // used in iMviewers.cc
  PetscErrorCode  getSurfaceValues(IceModelVec2 &gsurf, IceModelVec2 myH);
  PetscErrorCode  getSurfaceValues(IceModelVec2 &gsurf, PetscScalar **H);

protected:  
  PetscErrorCode  isLegalLevel(const PetscScalar z);
  virtual PetscErrorCode  define_netcdf_variable(int ncid, nc_type nctype, int *varidp);
};

#endif /* __IceModelVec_hh */

