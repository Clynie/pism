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

#include <cstring>
#include <ctime>
#include "iceModel.hh"
#include "pism_signal.h"
#include <petscvec.h>


//! Compute the scalar magnitude of a two-dimensional vector field.
PetscErrorCode IceModel::getMagnitudeOf2dVectorField(IceModelVec2 vfx, IceModelVec2 vfy,
						     IceModelVec2 vmag) {
  PetscErrorCode ierr;
  PetscScalar **fx, **fy, **mag;
  ierr = vfx.get_array(fx); CHKERRQ(ierr);
  ierr = vfy.get_array(fy); CHKERRQ(ierr);
  ierr = vmag.get_array(mag); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      mag[i][j] = sqrt(PetscSqr(fx[i][j]) + PetscSqr(fy[i][j]));
    }
  }
  ierr = vfx.end_access(); CHKERRQ(ierr);
  ierr = vfy.end_access(); CHKERRQ(ierr);
  ierr = vmag.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Virtual.  Does nothing in \c IceModel.  Derived classes can do more computation in each time step.
PetscErrorCode IceModel::additionalAtStartTimestep() {
  return 0;
}


//! Virtual.  Does nothing in \c IceModel.  Derived classes can do more computation in each time step.
PetscErrorCode IceModel::additionalAtEndTimestep() {
  return 0;
}

//! Catch signals -USR1 and -TERM; in the former case save and continue; in the latter, save and stop.
/*!
Signal \c SIGTERM makes PISM end, saving state under original \c -o name 
(or default name).  We also add an indication to the history attribute 
of the output NetCDF file.

Signal \c SIGUSR1 makes PISM save state under a filename based on the
the name of the executable (e.g. \c pismr or \c pismv) and the current 
model year.  There is no indication in the history attribute of the output 
NetCDF file because there is no effect.  There is an indication at \c stdout.
 */
int IceModel::endOfTimeStepHook() {
  
  if (pism_signal == SIGTERM) {
    verbPrintf(1, grid.com, 
       "Caught signal SIGTERM:  EXITING EARLY and saving with original filename.\n");
    char str[TEMPORARY_STRING_LENGTH];
    snprintf(str, sizeof(str), 
       "EARLY EXIT caused by signal SIGTERM.  Completed timestep at year=%.3f.",
       grid.year);
    stampHistory(str);
    return 1;
  }
  
  if (pism_signal == SIGUSR1) {
    char file_name[PETSC_MAX_PATH_LEN];
    snprintf(file_name, PETSC_MAX_PATH_LEN, "%s-%5.3f.nc",
             executable_short_name, grid.year);
    verbPrintf(1, grid.com, 
       "Caught signal SIGUSR1:  Writing intermediate file `%s'.\n",
       file_name);
    pism_signal = 0;
    dumpToFile(file_name);
  }

  return 0;
}


//! Build a history string from the command which invoked PISM.
PetscErrorCode  IceModel::stampHistoryCommand() {
  PetscErrorCode ierr;
  PetscInt argc;
  char **argv;
  
  ierr = PetscGetArgs(&argc, &argv); CHKERRQ(ierr);
  
  char cmdstr[TEMPORARY_STRING_LENGTH], startstr[TEMPORARY_STRING_LENGTH];

  snprintf(startstr, sizeof(startstr), 
           "PISM (%s) started on %d procs.", PISM_Revision, (int)grid.size);
  ierr = stampHistory(startstr); CHKERRQ(ierr);
  
//  strncpy(cmdstr, argv[0], sizeof(str)); // Does not null terminate on overflow
  strcpy(cmdstr, " ");
  strncat(cmdstr, argv[0], sizeof(cmdstr)); // Does not null terminate on overflow
  cmdstr[sizeof(cmdstr) - 1] = '\0';
  for (PetscInt i=1; i < argc; i++) {
    size_t remaining_bytes = sizeof(cmdstr) - strlen(cmdstr) - 1;
    // strncat promises to null terminate, so we must only make sure that the
    // end of the buffer is not overwritten.
    strncat(cmdstr, " ", remaining_bytes--);
    strncat(cmdstr, argv[i], remaining_bytes);
  }
  strcat(cmdstr,"\n");
  cmdstr[sizeof(cmdstr) - 1] = '\0';

  // All this hooplah is the C equivalent of the Ruby expression
  // ARGV.unshift($0).join(' ') and just ARGV.join(' ')
  // if the executable name was not needed.

  ierr = stampHistoryAdd(cmdstr); CHKERRQ(ierr);

  return 0;
}


//! Build the particular history string associated to the end of a PISM run.
PetscErrorCode  IceModel::stampHistoryEnd() {
  PetscErrorCode ierr;
  PetscLogDouble flops, my_flops;
  char str[TEMPORARY_STRING_LENGTH];
  MPI_Datatype mpi_type;

  ierr = PetscGetFlops(&my_flops); CHKERRQ(ierr);

  ierr = PetscDataTypeToMPIDataType(PETSC_DOUBLE, &mpi_type); CHKERRQ(ierr);
  MPI_Reduce(&my_flops, &flops, 1, mpi_type, MPI_SUM, 0, grid.com);
  
  snprintf(str, sizeof(str), "PISM done.  PETSc MFlops = %.2f.",
           flops * 1.0e-6);

  ierr = stampHistory(str); CHKERRQ(ierr);
  
  return 0;
}


//! Get time and user/host name and add it to the given string.  Then call stampHistoryAdd(). 
PetscErrorCode  IceModel::stampHistory(const char* string) {
  PetscErrorCode ierr;

  time_t now;
  tm tm_now;
  now = time(NULL);
  localtime_r(&now, &tm_now);

  char date_str[50];
  // Format specifiers for strftime():
  //   %F : ISO date format
  //   %T : Full 24 hour time
  //   %Z : Time Zone name
  //   %z : Time zone offset
  strftime(date_str, sizeof(date_str), "%F %T %Z", &tm_now);

  char username[50];
  ierr = PetscGetUserName(username, sizeof(username)); CHKERRQ(ierr);
  char hostname[100];
  ierr = PetscGetHostName(hostname, sizeof(hostname)); CHKERRQ(ierr);
  
  char str[TEMPORARY_STRING_LENGTH];
  int length = snprintf(str, sizeof(str), "%s@%s %s:  %s\n",
                        username, hostname, date_str, string);
  
  if (length < 0) {
    SETERRQ(1, "Output error or snprintf() is not C99 compliant.");
    // Old implementations of snprintf() will return `-1' when the string is
    // truncated.  If this is the case on some platform, we need to change this
    // check to allow for that possibility.
  }
  if (length > (int)sizeof(str)) {
    ierr = PetscPrintf(grid.com,
       "Warning: command line truncated by %d chars in history.\n",
       length + 1 - sizeof(str)); CHKERRQ(ierr);
    str[sizeof(str) - 2] = '\n';
    str[sizeof(str) - 1] = '\0';
  }

  ierr = stampHistoryAdd(str); CHKERRQ(ierr);
  
  return 0;
}


//! Add the given string to the history data member in IceModel.
PetscErrorCode  IceModel::stampHistoryAdd(const char* string) {
  size_t historyLength = strlen(history);
  size_t stringLength = strlen(string);
  char *tempstr;

  if (stringLength + historyLength > history_size - 1)
    history_size += stringLength + 1;

  tempstr = new char[history_size];

  //prepend it; this matches NCO behavior so commands are in order
  strcpy(tempstr, string);
  strcat(tempstr, history);

  delete[] history;
  
  history = tempstr;
  
  return 0;
}


//! Check if the thickness of the ice is so large that ice is above the top of the computational grid.  FIXME: This is the place to automatically expand 3D computational grid (task #4218).
PetscErrorCode IceModel::thicknessTooLargeCheck() {
  PetscErrorCode  ierr;

  PetscScalar **H;
  ierr = vH.get_array(H); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (H[i][j] > grid.Lz) {
        PetscPrintf(grid.com,
           "PISM ERROR thicknessTooLargeCheck(): ice thickness exceeds computational box;\n"
           "  H[i][j] = %5.4f exceeds Lz = %5.4f  ... ENDING!!\n",
           H[i][j], grid.Lz);
        PetscEnd();
      }
    }
  }
  ierr = vH.end_access(); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceModel::report_grid_parameters() {
  PetscErrorCode ierr;

  // report on computational box
  ierr = verbPrintf(2,grid.com, 
           "[computational box for ice: %8.2f km x %8.2f km",
           2*grid.Lx/1000.0,2*grid.Ly/1000.0); CHKERRQ(ierr);
  if (grid.Mbz > 1) {
    ierr = verbPrintf(2,grid.com,
         "\n                               x (%8.2f m + %7.2f m bedrock)]\n"
         ,grid.Lz,grid.Lbz); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(2,grid.com," x %8.2f m]\n",grid.Lz); CHKERRQ(ierr);
  }
  
  // report on grid cell dims
  if (grid.vertical_spacing == EQUAL) {
    ierr = verbPrintf(2,grid.com, 
           "[grid cell dims (equal dz): %8.2f km x %8.2f km x %8.2f m",
           grid.dx/1000.0,grid.dy/1000.0,grid.dzMIN); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(2,grid.com, 
           "[hor. grid cell dimensions: %8.2f km x %8.2f km\n",
           grid.dx/1000.0,grid.dy/1000.0); CHKERRQ(ierr);
    ierr = verbPrintf(2,grid.com, 
           " vertical grid spacing in ice not equal; range %.3f m < dz < %.3f m",
           grid.dzMIN,grid.dzMAX); CHKERRQ(ierr);
    PetscInt    myMz, dummyM;
    ierr = getMzMbzForTempAge(myMz,dummyM); CHKERRQ(ierr);
    ierr = verbPrintf(3,grid.com, 
         "\n fine equal spacing used in temperatureStep(): Mz = %d, dzEQ = %.3f m",
           myMz,grid.Lz / ((PetscScalar) (myMz - 1))); CHKERRQ(ierr);
    if (myMz > 1000) {
      ierr = verbPrintf(1,grid.com,
        "\n\n WARNING: Using more than 1000 vertical levels internally\n"
        "   in temperatureStep()!\n\n");  CHKERRQ(ierr);
    }
  }

  if (grid.Mbz > 1) {
    ierr = verbPrintf(2,grid.com, 
       "\n vertical spacing in bedrock: dz = %.3f m]\n",
         grid.zblevels[1]-grid.zblevels[0]); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(2,grid.com,"]\n"); CHKERRQ(ierr);
  }

  // if -verbose (=-verbose 3) then actually list parameters of grid
  ierr = verbPrintf(3,grid.com,
         "  [grid parameters list (verbose output):\n"); CHKERRQ(ierr);
  ierr = verbPrintf(3,grid.com,
         "            x0 = %6.2f km, y0 = %6.2f km,\n",
		    grid.x0/1000.0, grid.y0/1000.0); CHKERRQ(ierr);
  ierr = verbPrintf(3,grid.com,
         "            Mx = %d, My = %d, Mz = %d, Mbz = %d,\n",
         grid.Mx,grid.My,grid.Mz,grid.Mbz); CHKERRQ(ierr);
  ierr = verbPrintf(3,grid.com,
         "            Lx = %6.2f km, Ly = %6.2f m, Lz = %6.2f m, Lbz = %6.2f m,\n",
         grid.Lx/1000.0,grid.Ly/1000.0,grid.Lz,grid.Lbz); CHKERRQ(ierr);
  ierr = verbPrintf(3,grid.com,
         "            dx = %6.3f km, dy = %6.3f km, year = %8.4f]\n",
         grid.dx/1000.0,grid.dy/1000.0,grid.year); CHKERRQ(ierr);

  // if -verbose 5 then more stuff
  ierr = verbPrintf(5,grid.com,
       "\n  [vertical levels (REALLY verbose output):\n"); CHKERRQ(ierr);
  ierr = grid.printVertLevels(5); CHKERRQ(ierr);  // only if verbose 5
  ierr = verbPrintf(5,grid.com,"]\n"); CHKERRQ(ierr);
  
  return 0;
}

//! Print a warning telling the user that an option was ignored.
PetscErrorCode IceModel::ignore_option(const char name[]) {
  PetscErrorCode ierr;
  PetscTruth option_is_set;

  char tmp[TEMPORARY_STRING_LENGTH]; // dummy string
  ierr = PetscOptionsGetString(PETSC_NULL, name, tmp, TEMPORARY_STRING_LENGTH,
			       &option_is_set); CHKERRQ(ierr);

  if (option_is_set) {
    ierr = verbPrintf(1, grid.com,
		      "PISM WARNING: ignoring command-line option '%s'.\n",
		      name); CHKERRQ(ierr);
  }

  return 0;
}

PetscErrorCode IceModel::check_old_option_and_stop(const char old_name[], const char new_name[]) {
  PetscErrorCode ierr;
  PetscTruth option_is_set;

  char tmp[TEMPORARY_STRING_LENGTH]; // dummy string
  ierr = PetscOptionsGetString(PETSC_NULL, old_name, tmp, TEMPORARY_STRING_LENGTH,
			       &option_is_set); CHKERRQ(ierr);

  if (option_is_set) {
    ierr = PetscPrintf(grid.com,
		       "PISM ERROR: command-line option '%s' is deprecated. Please use '%s' instead.\n",
		       old_name, new_name); CHKERRQ(ierr);
    PetscEnd();
  }

  return 0;
}

PetscErrorCode IceModel::stop_if_set(const char name[]) {
  PetscErrorCode ierr;
  PetscTruth option_is_set;

  char tmp[TEMPORARY_STRING_LENGTH]; // dummy string
  ierr = PetscOptionsGetString(PETSC_NULL, name, tmp, TEMPORARY_STRING_LENGTH,
			       &option_is_set); CHKERRQ(ierr);

  if (option_is_set) {
    ierr = PetscPrintf(grid.com,
		       "PISM ERROR: command-line option '%s' is not allowed.\n",
		       name); CHKERRQ(ierr);
    PetscEnd();
  }

  return 0;
}
