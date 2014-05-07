// Copyright (C) 2012, 2013, 2014 PISM Authors
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

#ifndef _PISMGREGORIANTIME_H_
#define _PISMGREGORIANTIME_H_

#include "PISMTime.hh"
#include "PISMUnits.hh"

namespace pism {

class Time_Calendar : public Time
{
public:
  Time_Calendar(MPI_Comm c, const Config &conf,
                const std::string &calendar,
                const UnitSystem &units_system);
  virtual ~Time_Calendar();

  virtual PetscErrorCode init();

  virtual PetscErrorCode init_from_file(const std::string &filename);

  virtual double mod(double time, unsigned int);

  virtual double year_fraction(double T);

  virtual std::string date(double T);

  virtual std::string date();

  virtual std::string start_date();

  virtual std::string end_date();

  virtual std::string units_string() {
    return CF_units_string();
  }

  virtual std::string CF_units_string() {
    return m_time_units.format();
  }

  virtual std::string CF_units_to_PISM_units(const std::string &input) {
    return input;               // return unchanged CF units
  }

  virtual bool use_reference_date()
  { return true; }

  virtual double calendar_year_start(double T);

  virtual double increment_date(double T, int years);

protected:
  virtual PetscErrorCode compute_times(double time_start, double delta, double time_end,
                                       const std::string &keyword,
                                       std::vector<double> &result);

  virtual PetscErrorCode process_ys(double &result, bool &flag);
  virtual PetscErrorCode process_y(double &result, bool &flag);
  virtual PetscErrorCode process_ye(double &result, bool &flag);

  virtual PetscErrorCode parse_date(const std::string &spec, double *result);

  virtual PetscErrorCode parse_interval_length(const std::string &spec,
                                               std::string &keyword, double *result);

  PetscErrorCode compute_times_monthly(std::vector<double> &result);

  PetscErrorCode compute_times_yearly(std::vector<double> &result);
private:
  // Hide copy constructor / assignment operator.
  Time_Calendar(Time_Calendar const &);
  Time_Calendar & operator=(Time_Calendar const &);
};


} // end of namespace pism

#endif /* _PISMGREGORIANTIME_H_ */
