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

#ifndef __grid_hh
#define __grid_hh

#include <petscda.h>

typedef enum {UNKNOWN = 0, EQUAL, CHEBYSHEV, QUADRATIC} SpacingType;
typedef enum {NONE = 0, X_PERIODIC = 1, Y_PERIODIC = 2, XY_PERIODIC = 3} Periodicity;

//! Describes the PISM grid and the distribution of data across processors.
/*!
This class holds parameters describing the grid, including the vertical spacing 
and which part of the horizontal grid is owned by the processor.  It contains 
the dimensions of the PISM computational box.

It creates and destroys a two dimensional \c PETSc \c DA (distributed array).
The creation of this \c DA is the point at which PISM gets distributed across 
multiple processors.
 */
class IceGrid {
public:
  IceGrid(MPI_Comm c, PetscMPIInt r, PetscMPIInt s);
  ~IceGrid();

  PetscErrorCode createDA();  // destructor checks if DA was created, and destroys
  PetscErrorCode set_vertical_levels(int Mz, int Mbz, double *z_levels, double *zb_levels);
  PetscErrorCode compute_vertical_levels();
  PetscErrorCode compute_horizontal_spacing();
  PetscErrorCode compute_horizontal_coordinates(double* &x, double* &y);
  PetscErrorCode printVertLevels(int verbosity); 
  PetscInt       kBelowHeight(PetscScalar height);

  
  MPI_Comm    com;
  PetscMPIInt rank, size;
  DA          da2;		// whether this is PETSC_NULL is important;
				// functions such as IceModelVec::read_from_netcdf() use it
				// to determine if values in this DA can be trusted
  PetscInt    xs, xm, ys, ym;

  PetscScalar *zlevels, *zblevels; // z levels, in ice & bedrock; the storage grid for fields 
                                   // which are represented in 3d Vecs
  SpacingType vertical_spacing;
  Periodicity periodicity;
  PetscScalar dzMIN, dzMAX;

  PetscScalar x0, y0;	   // grid center (from an input or bootstrapping file)
  PetscScalar Lx, Ly; // half width of the ice model grid in x-direction, y-direction (m)
  PetscInt    Mx, My; // number of grid points in x-direction, y-direction
  PetscScalar dx, dy; // horizontal grid spacing

  PetscScalar Lz, Lbz; // extent of the ice, bedrock in z-direction (m)
  PetscInt    Mz, Mbz; // number of grid points in z-direction, ice and bedrock.

  PetscScalar year;       // current time (years)
  
protected:
  static const SpacingType DEFAULT_SPACING_TYPE;
  static const Periodicity DEFAULT_PERIODICITY;
  static const PetscScalar DEFAULT_QUADZ_LAMBDA;
  // FORMERLY IN IceParam:
  static const PetscScalar DEFAULT_ICEPARAM_Lx, DEFAULT_ICEPARAM_Ly, DEFAULT_ICEPARAM_Lz, 
                           DEFAULT_ICEPARAM_year;
  static const PetscInt    DEFAULT_ICEPARAM_Mx, DEFAULT_ICEPARAM_My, DEFAULT_ICEPARAM_Mz,
                           DEFAULT_ICEPARAM_Mbz;
private:
  PetscErrorCode  get_dzMIN_dzMAX_spacingtype();
  bool            isIncreasing(const PetscInt len, PetscScalar *vals);
};

#endif	/* __grid_hh */

