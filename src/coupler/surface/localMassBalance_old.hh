// Copyright (C) 2009, 2010, 2011, 2014, 2015 Ed Bueler and Constantine Khroulev
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

#ifndef __localMassBalance_old_hh
#define __localMassBalance_old_hh

#include <gsl/gsl_rng.h>

#include "base/util/iceModelVec.hh"  // only needed for FaustoGrevePDDObject
#include "base/util/PISMConfigInterface.hh"

namespace pism {

namespace surface {
//! \brief Base class for a model which computes surface mass flux rate (ice
//! thickness per time) from precipitation and temperature.
/*!
  This is a process model.  At each spatial location, it uses a 1D array, with a
  time dimension, for the temperature used in melting snow or ice.  At each spatial
  location it assumes the precipitation is time-independent.

  This process model does not know its location on the ice sheet, but
  simply computes the surface mass balance from three quantities:
  - the time interval \f$[t,t+\Delta t]\f$,
  - time series of values of surface temperature at \f$N\f$ equally-spaced
  times in the time interval
  - a scalar precipation rate which is taken to apply in the time interval.

  This model also uses degree day factors passed-in in DegreeDayFactors \c ddf,
  and the standard deviation \c pddStdDev.  The latter is the standard deviation of the
  modeled temperature away from the input temperature time series which contains
  the part of location-dependent temperature cycle on the time interval.

  @note 
  - Please avoid using `config.get...("...")` calls
  inside those methods of this class which are called inside loops over 
  spatial grids.  Doing otherwise increases computational costs.
  - This base class should be more general.  For instance, it could allow as
  input a time series for precipation rate.
*/
class LocalMassBalance_Old {
public:

  //! A struct which holds degree day factors.
  /*!
    Degree day factors convert positive degree days (=PDDs) into amount of melt.
  */
  struct DegreeDayFactors {
    double  snow, //!< m day^-1 K^-1; ice-equivalent amount of snow melted, per PDD
      ice,  //!< m day^-1 K^-1; ice-equivalent amount of ice melted, per PDD
      refreezeFrac;  //!< fraction of melted snow which refreezes as ice
  };

  LocalMassBalance_Old(Config::ConstPtr myconfig)
    : config(myconfig) {}
  virtual ~LocalMassBalance_Old() {}
  virtual void init() {
    // empty
  };

  /*! Call before getMassFluxFromTemperatureTimeSeries() so that mass balance method can
    decide how to cut up the time interval.  Most implementations will ignore
    t and just use dt.  Input t,dt in seconds.  */
  virtual void getNForTemperatureSeries(double t, double dt, int &N) = 0;

  //! Count positive degree days (PDDs).  Returned value in units of K day.
  /*! Inputs T[0],...,T[N-1] are temperatures (K) at times t, t+dt_series, ..., t+(N-1)dt_series.
    Inputs \c t, \c dt_series are in seconds.  */
  virtual double getPDDSumFromTemperatureTimeSeries(double pddStdDev, double pddThresholdTemp,
                                                    double t, double dt_series, double *T, int N) = 0;

  /*! Remove rain from precip.  Returned value is amount of snow in ice-equivalent m. */
  /*! Inputs \c precip_rate is in ice-equivalent m s-1.  Note
    <tt>dt = N * dt_series</tt> is the full time-step.  */
  virtual double getSnowFromPrecipAndTemperatureTimeSeries(double precip_rate,
                                                           double t, double dt_series, double *T, int N) = 0;

  /*! Input \c dt is in seconds.  Input \c pddsum is in K day.  
    Input \c snow is in ice-equivalent m. 
    Returned mass fluxes, including \c accumulation_rate, \c melt_rate,
    \c runoff_rate, and \c smb (= surface mass balance), are in 
    ice-equivalent thickness per time (m s-1).  */
  virtual void getMassFluxesFromPDDs(const DegreeDayFactors &ddf,
                                     double dt, double pddsum,
                                     double snow,
                                     double &accumulation_rate,
                                     double &melt_rate,
                                     double &runoff_rate,
                                     double &smb_rate) = 0;

protected:
  const Config::ConstPtr  config;
};


//! A PDD implementation which computes the local mass balance based on an expectation integral.
/*!
  The expected number of positive degree days is computed by an integral in \ref CalovGreve05.

  Uses degree day factors which are location-independent.
*/
class PDDMassBalance_Old : public LocalMassBalance_Old {

public:
  PDDMassBalance_Old(Config::ConstPtr myconfig);
  virtual ~PDDMassBalance_Old() {}

  virtual void getNForTemperatureSeries(double t, double dt, int &N);

  virtual double getPDDSumFromTemperatureTimeSeries(double pddStdDev, double pddThresholdTemp,
                                                    double t, double dt_series, double *T, int N);

  virtual double getSnowFromPrecipAndTemperatureTimeSeries(double precip_rate,
                                                           double t, double dt_series, double *T, int N);

  virtual void getMassFluxesFromPDDs(const LocalMassBalance_Old::DegreeDayFactors &ddf,
                                     double dt, double pddsum,
                                     double snow,
                                     double &accumulation_rate,
                                     double &melt_rate,
                                     double &runoff_rate,
                                     double &smb_rate);

protected:
  double CalovGreveIntegrand(double sigma, double TacC);

  bool precip_as_snow;          //!< interpret all the precipitation as snow (no rain)
  double Tmin,             //!< the temperature below which all precipitation is snow
    Tmax;             //!< the temperature above which all precipitation is rain

  double secpera;
};


//! An alternative PDD implementation which simulates a random process to get the number of PDDs.
/*!
  Uses a GSL random number generator.  Significantly slower because new random numbers are
  generated for each grid point.

  The way the number of positive degree-days are used to produce a surface mass balance
  is identical to the base class PDDMassBalance_Old.

  \note
  \li A more realistic pattern for the variability of surface melting might have correlation 
  with appropriate spatial and temporal ranges.
*/
class PDDrandMassBalance_Old : public PDDMassBalance_Old {

public:
  PDDrandMassBalance_Old(Config::ConstPtr myconfig, bool repeatable); //! repeatable==true to seed with zero every time.
  virtual ~PDDrandMassBalance_Old();

  virtual void getNForTemperatureSeries(double t, double dt, int &N);

  virtual double getPDDSumFromTemperatureTimeSeries(double pddStdDev, double pddThresholdTemp,
                                                    double t, double dt_series, double *T, int N);

protected:
  gsl_rng *pddRandGen;
};


/*!
  The PDD scheme described by Formula (6) in [\ref Faustoetal2009] requires 
  special knowledge of latitude and mean July temp to set degree day factors 
  for Greenland.

  These formulas are inherited by [\ref Faustoetal2009] from [\ref Greve2005geothermal].
  There was, apparently, tuning in [\ref Greve2005geothermal] which mixed ice
  dynamical ideas and surface process ideas.  That is, these formulas and parameter
  choices arise from looking at margin shape.  This may not be a good source of
  PDD parameters.

  This may become a derived class of a LocationDependentPDDObject, if the idea
  is needed more in the future.
*/
class FaustoGrevePDDObject_Old {

public:
  FaustoGrevePDDObject_Old(IceGrid::ConstPtr g);
  virtual ~FaustoGrevePDDObject_Old() {}

  virtual void update_temp_mj(const IceModelVec2S &surfelev,
                              const IceModelVec2S &lat,
                              const IceModelVec2S &lon);

  /*! If this method is called, it is assumed that i,j is in the ownership range
    for IceModelVec2S temp_mj. */
  virtual void setDegreeDayFactors(int i, int j,
                                   double /* usurf */,
                                   double lat,
                                   double /* lon */,
                                   LocalMassBalance_Old::DegreeDayFactors &ddf);

protected:
  IceGrid::ConstPtr grid;
  const Config::ConstPtr config;
  double beta_ice_w, beta_snow_w, T_c, T_w, beta_ice_c, beta_snow_c,
    fresh_water_density, ice_density, pdd_fausto_latitude_beta_w;
  IceModelVec2S temp_mj;
};

} // end of namespace surface
} // end of namespace pism

#endif
