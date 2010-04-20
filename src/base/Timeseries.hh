// Copyright (C) 2009 Constantine Khroulev
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

#ifndef __Timeseries_hh
#define __Timeseries_hh

#include "NCVariable.hh"
#include "grid.hh"
#include "nc_util.hh"
#include <deque>

// use namespace std BUT remove trivial namespace browser from doxygen-erated HTML source browser
/// @cond NAMESPACE_BROWSER
using namespace std;
/// @endcond


//! A general class for reading and accessing time-series.
/*!
  Provides random access to time-series values.

  Note that every processor stores the whole time-series and calling append()
  repeatedly will use a lot of memory.

  Please use DiagnosticTimeseries to output long time-series.
 */
class Timeseries {
public:
  Timeseries(IceGrid * g, string name, string dimension_name);
  Timeseries(MPI_Comm com, PetscMPIInt rank, string name, string dimension_name);
  
  PetscErrorCode read(const char filename[]);
  PetscErrorCode write(const char filename[]);
  double operator()(double time);
  double operator[](unsigned int j) const;
  PetscErrorCode append(double time, double value);
  int length();
  PetscErrorCode set_attr(string name, double value);
  PetscErrorCode set_attr(string name, string value);
  PetscErrorCode set_units(string units, string glaciological_units);
  PetscErrorCode set_dimension_units(string units, string glaciological_units);

  string short_name;
protected:
  MPI_Comm com;
  PetscMPIInt rank;
  NCTimeseries dimension, var;

  vector<double> time;
  vector<double> values;
};

//! A class for storing and writing diagnostic time-series.
/*! This version of Timeseries only holds \c buffer_size entries in memory and
  writes to a file every time this limit is exceeded.

  Here is a usage example:

  First, prepare a file for writing:

  \code
  char seriesname[] = "ser_delta_T.nc";
  NCTool nc(grid.com, grid.rank);
  nc.open_for_writing(seriesname, true, false);
  nc.close();
  \endcode

  Next, create the DiagnosticTimeseries object and set metadata. This will
  prepare the offsets object to write delta_T(t) time-series to file
  ser_delta_T.nc, converting from degrees Celsius (internal units) to degrees
  Kelvin ("glaciological" units). Time will be written in years (%i.e. there is
  no unit conversion there).

  \code
  offsets = new DiagnosticTimeseries(g, "delta_T", "t");
  offsets->set_units("Kelvin", "Celsius");
  offsets->set_dimension_units("years", "");
  offsets->buffer_size = 100; // only store 100 entries; default is 10000
  offsets->output_filename = seriesname;
  offsets->set_attr("long_name", "temperature offsets from some value");
  \endcode

  Once this is set up, one can add calls like

  \code
  offsets->append(t_years, TsOffset);
  offsets->interp(time);
  \endcode

  to the code. The first call will store the (t_years, TsOffset). The second
  call will use linear interpolation to find the value at \c time years.  Note
  that the first call adds to a buffer but does not yield any output without 
  the second call.  Therefore, even if interpolation is not really needed
  because time==t_years, the call to interp() should still occur.
  
  Finally, the destructor of DiagnosticTimeseries will flush(), which writes out
  the buffered values:

  \code
  delete offsets;
  \endcode

  Note that every time you exceed the \c buffer_size limit, all the entries are
  written to a file by flush() <b> and removed from memory</b>.  One may also
  explicitly call flush().
 */
class DiagnosticTimeseries : public Timeseries {
public:
  DiagnosticTimeseries(IceGrid * g, string name, string dimension_name);
  DiagnosticTimeseries(MPI_Comm com, PetscMPIInt rank, string name, string dimension_name);
  ~DiagnosticTimeseries();

  PetscErrorCode append(double T, double V);
  PetscErrorCode interp(double time);
  PetscErrorCode flush();

  size_t buffer_size;
  string output_filename;

protected:
  size_t start;
  deque<double> t, v;
};

#endif // __Timeseries_hh
