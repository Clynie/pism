// Copyright (C) 2011 Constantine Khroulev
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

#ifndef _PSCALARFORCING_H_
#define _PSCALARFORCING_H_

#include "PISMSurface.hh"
#include "PISMAtmosphere.hh"
#include "PISMOcean.hh"

template<class Model, class Mod>
class PScalarForcing : public Mod
{
public:
  PScalarForcing(IceGrid &g, const NCConfigVariable &conf, Model* in)
    : Mod(g, conf, in), input(in) {}
  virtual ~PScalarForcing()
  {
    if (offset)
      delete offset;
  }

  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years)
  {
    Mod::t = my_mod(t_years);
    Mod::dt = dt_years;

    PetscErrorCode ierr = Mod::input_model->update(t_years, dt_years); CHKERRQ(ierr);
    return 0;
  }

protected:
  virtual PetscErrorCode init_internal()
  {
    PetscErrorCode ierr;
    bool option_set, bc_period_set, bc_ref_year_set;

    IceGrid &g = Mod::grid;

    bc_period = 0;
    bc_reference_year = 0;

    ierr = PetscOptionsBegin(g.com, "", "Scalar forcing options", ""); CHKERRQ(ierr);
    {
      ierr = PISMOptionsString(option, "Specifies a file with scalar offsets",
                               filename, option_set); CHKERRQ(ierr);
      ierr = PISMOptionsReal(option + "_period", "Specifies the length of the climate data period",
                             bc_period, bc_period_set); CHKERRQ(ierr);
      ierr = PISMOptionsReal(option + "_reference_year", "Boundary condition reference year",
                             bc_reference_year, bc_ref_year_set); CHKERRQ(ierr);
    }
    ierr = PetscOptionsEnd(); CHKERRQ(ierr);

    if (option_set == false) {
      ierr = verbPrintf(2, g.com, "  WARNING: %s is not set; forcing is inactive.\n",
                        option.c_str()); CHKERRQ(ierr);
      delete offset;
      offset = NULL;
    }

    if (offset) {
      ierr = verbPrintf(2, g.com, 
                        "  reading %s data from forcing file %s...\n",
                        offset->short_name.c_str(), filename.c_str());
      CHKERRQ(ierr);

      ierr = offset->read(filename.c_str()); CHKERRQ(ierr);
    }

    return 0;
  }

  PetscErrorCode offset_data(IceModelVec2S &result) {
    if (offset) {
      PetscErrorCode ierr = result.shift((*offset)(Mod::t + 0.5*Mod::dt)); CHKERRQ(ierr);
    }
    return 0;
  }

  //! \brief Computes year modulo bc_period if bc_period is active.
  PetscReal my_mod(PetscReal in) {
    if (bc_period < 1e-6) return in;

    // compute time since the reference year:
    PetscReal delta = in - bc_reference_year;

    // compute delta mod bc_period:
    return delta - floor(delta / bc_period) * bc_period;
  }

  Model *input;
  Timeseries *offset;
  string filename, offset_name, option;

  PetscReal bc_period, bc_reference_year;

};

class PSdTforcing : public PScalarForcing<PISMSurfaceModel,PSModifier>
{
public:
  PSdTforcing(IceGrid &g, const NCConfigVariable &conf, PISMSurfaceModel* in);
  virtual ~PSdTforcing() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result);
};

class PAdTforcing : public PScalarForcing<PISMAtmosphereModel,PAModifier>
{
public:
  PAdTforcing(IceGrid &g, const NCConfigVariable &conf, PISMAtmosphereModel* in);
  virtual ~PAdTforcing() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual PetscErrorCode mean_annual_temp(IceModelVec2S &result);

  virtual PetscErrorCode temp_time_series(int i, int j, int N,
                                          PetscReal *ts, PetscReal *values);
  virtual PetscErrorCode temp_snapshot(IceModelVec2S &result);

};

class POdSLforcing : public PScalarForcing<PISMOceanModel,POModifier>
{
public:
  POdSLforcing(IceGrid &g, const NCConfigVariable &conf, PISMOceanModel* in);
  virtual ~POdSLforcing() {}

  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode sea_level_elevation(PetscReal &result);
};

#endif /* _PSCALARFORCING_H_ */
