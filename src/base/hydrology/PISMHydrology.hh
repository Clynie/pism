// Copyright (C) 2012-2013 PISM Authors
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

#ifndef _PISMHYDROLOGY_H_
#define _PISMHYDROLOGY_H_

#include <assert.h>

#include "iceModelVec.hh"
#include "iceModelVec2T.hh"
#include "PISMComponent.hh"
#include "PISMStressBalance.hh"


//! \brief The PISM subglacial hydrology model interface.
/*!
This is a virtual base class.

The purpose of this class and its derived classes is to provide
\code
  subglacial_water_thickness(IceModelVec2S &result)
  subglacial_water_pressure(IceModelVec2S &result)
  englacial_water_thickness(IceModelVec2S &result)
  till_water_thickness(IceModelVec2S &result)
  till_water_pressure(IceModelVec2S &result)
\endcode

Additional modeled fields, for diagnostic purposes, are
\code
  overburden_pressure(IceModelVec2S &result)
  wall_melt(IceModelVec2S &result)
\endcode

This interface is specific to subglacial hydrology models which track a
two-dimensional water layer with a well-defined thickness and pressure at each
map-plane location.  The methods subglacial_water_thickness() and
subglacial_water_pressure() return amount and pressure.  This subglacial water
is *transportable*, that is, it moves along a modeled hydraulic head gradient.
For more information see [\ref vanPeltBuelerDRAFT].  Background references for
such models include [\ref FlowersClarke2002_theory, \ref Hewittetal2012,
\ref Schoofetal2012].

These models also either track a separate, but coupled, amount of water which
is held in local till storage, or they lack that mechanism.  If they lack the
mechanism then till_water_thickness() returns zero while till_water_pressure()
returns subglacial_water_pressure().  If they have the mechanism then
generally the subglacial and till pressures are different.  The till pressure
is used by the Mohr-Coulomb criterion to provide a yield stress.  References
for such models with till storage include [\ref BBssasliding,  
\ref SchoofTill, \ref TrufferEchelmeyerHarrison, \ref Tulaczyketal2000b].

These models also either track the amount of englacial water, in a manner
which allows computation of an effective thickness and which is returned by
englacial_water_thickness(), or they lack the mechanism and
englacial_water_thickness() returns zero.  A reference for such a model with
englacial storage is [\ref Bartholomausetal2011].

PISMHydrology is a timestepping component (PISMComponent_TS).  Because of the
short physical timescales associated to liquid water moving under a glacier,
PISMHydrology derived classes generally take many substeps in PISM's major
ice dynamics time steps.  Thus when an update() method in a PISMHydrology
derived class is called it will advance its internal time to the new goal t+dt
using its own internal time steps.

Generally PISMHydrology and derived classes use the ice geometry, the basal melt
rate, and the basal sliding velocity.  Note that the basal melt rate is an
energy-conservation-derived field.  These fields generally
come from IceModel and PISMStressBalance.  Additionally, time-dependent
and spatially-variable water input to the basal layer, taken directly from a
file, is possible too.  Potentially PISMSurfaceModel could supply such a
quantity.

Ice geometry and energy fields are normally treated as constant in time
during the update() call for the interval [t,t+dt].  Thus the coupling is
one-way during the update() call.
 */
class PISMHydrology : public PISMComponent_TS {
public:
  PISMHydrology(IceGrid &g, const NCConfigVariable &conf);
  virtual ~PISMHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual PetscErrorCode regrid(IceModelVec2S &myvar);

  virtual void get_diagnostics(map<string, PISMDiagnostic*> &dict);
  friend class PISMHydrology_hydroinput;

  virtual PetscErrorCode max_timestep(PetscReal my_t, PetscReal &my_dt, bool &restrict_dt);

  // derived classes need to have a model state, which will be the variables in this set:
  virtual void add_vars_to_output(string keyword, set<string> &result) = 0;
  virtual PetscErrorCode define_variables(set<string> vars, const PIO &nc,PISM_IO_Type nctype) = 0;
  virtual PetscErrorCode write_variables(set<string> vars, const PIO &nc) = 0;

  // derived classes need to be able to update their internal state
  using PISMComponent_TS::update;
  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt) = 0;

  // regardless of the derived class model state variables, these methods
  // need to be implemented so that PISMHydrology is useful to the outside
  virtual PetscErrorCode subglacial_water_thickness(IceModelVec2S &result) = 0;
  virtual PetscErrorCode subglacial_water_pressure(IceModelVec2S &result) = 0;

  // these two exist in the base class and set result = 0:
  virtual PetscErrorCode englacial_water_thickness(IceModelVec2S &result);
  virtual PetscErrorCode till_water_thickness(IceModelVec2S &result);

  // this method needs to be implemented by the derived class
  virtual PetscErrorCode till_water_pressure(IceModelVec2S &result) = 0;

  // this diagnostic method returns the standard shallow approximation
  virtual PetscErrorCode overburden_pressure(IceModelVec2S &result);

  // this diagnostic method returns zero in the base class
  virtual PetscErrorCode wall_melt(IceModelVec2S &result);

protected:
  // this model's workspace
  IceModelVec2S total_input;
  // pointers into IceModel; these describe the ice sheet and the source
  IceModelVec2S *thk,   // ice thickness
                *bed,   // bed elevation (not all models need this)
                *cellarea, // projection-dependent area of each cell, used in mass reporting
                *bmelt; // ice sheet basal melt rate
  IceModelVec2Int *mask;// floating, grounded, etc. mask
  IceModelVec2T *inputtobed;// time dependent input of water to bed, in addition to bmelt
  PetscReal     inputtobed_period, inputtobed_reference_time;
  PISMVars *variables;
  bool report_mass_accounting;
  virtual PetscErrorCode get_input_rate(
                            PetscReal hydro_t, PetscReal hydro_dt, IceModelVec2S &result);
  virtual PetscErrorCode boundary_mass_changes(IceModelVec2S &Wnew,
                            PetscReal &icefreelost, PetscReal &oceanlost, PetscReal &negativegain);
};


//! \brief The subglacial hydrology model from Bueler & Brown (2009) WITHOUT contrived water diffusion.
/*!
The name "till-can" comes from the following mental image:  Each map-plane cell
under the glacier or ice sheet does not communicate with the next cell; i.e.
there are "can walls" separating the cells.  The cans are "open-topped" in the
sense that they fill up to level bwat_max.  Any water exceeding bwat_max "spills
over the sides" \e and \e disappears.  Thus this model is not mass conserving.
It is useful for computing a till yield stress based on a time-integrated basal
melt rate.

See [\ref BBssasliding] and [\ref Tulaczyketal2000b].  See this URL for a talk
where the "till-can" metaphor is illustrated:
  http://www2.gi.alaska.edu/snowice/glaciers/iceflow/bueler-igs-fairbanks-june2012.pdf

The paper [\ref BBssasliding] used this model but with contrived diffusion of
the water.  It is implemented in the derived class PISMDiffuseOnlyHydrology.
 */
class PISMTillCanHydrology : public PISMHydrology {
public:
  PISMTillCanHydrology(IceGrid &g, const NCConfigVariable &conf, bool Whasghosts);
  virtual ~PISMTillCanHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual void add_vars_to_output(string keyword, set<string> &result);
  virtual PetscErrorCode define_variables(set<string> vars, const PIO &nc,PISM_IO_Type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, const PIO &nc);

  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt);

  virtual PetscErrorCode subglacial_water_thickness(IceModelVec2S &result);
  virtual PetscErrorCode subglacial_water_pressure(IceModelVec2S &result);

protected:
  // this model's state
  IceModelVec2S W;      // water layer thickness

  virtual PetscErrorCode allocate(bool Whasghosts);
  virtual PetscErrorCode check_W_bounds();

  //! \brief Updates the basal water layer thickness (in meters) at a grid cell.
  /*!
   * @param[in] my_W water layer thickness before the update
   * @param[in] dWinput change in water amount due do melt/refreeze (can be of either sign)
   * @param[in] dWdecay change in water amount due to the "decay" mechanism (non-negative)
   * @param[in] Wmax maximum allowed water layer thickness
   *
   * @returns The new basal water thickness, W.  Note that W
   * computed here may be negative due to refreeze, but not due to the gradual decay.
   */
  inline PetscReal pointwise_update(PetscReal my_W, PetscReal dWinput, PetscReal dWdecay, PetscReal Wmax) {
    assert(dWdecay >= 0);

    my_W += dWinput;       // if this makes my_W negative then we leave it for reporting

    // avoids having the decay rate contribution reported as an icefree or floating mass loss:
    if (dWdecay < my_W)    // case where my_W is largish and decay rate reduces it
      my_W -= dWdecay;
    else if (my_W >= 0.0)  // case where decay rate would go past zero ... don't allow that
      my_W = 0.0;

    return PetscMin(Wmax, my_W);  // overflows top of "can" and we lose it
  }
};


//! \brief The subglacial hydrology model from Bueler & Brown (2009) WITH contrived water diffusion.
/*!
Implements the full model in [\ref BBssasliding], including the diffusion in
equation (11).
 */
class PISMDiffuseOnlyHydrology : public PISMTillCanHydrology {
public:
  PISMDiffuseOnlyHydrology(IceGrid &g, const NCConfigVariable &conf);
  virtual ~PISMDiffuseOnlyHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt);

protected:
  IceModelVec2S Wnew;  // new value at update
  virtual PetscErrorCode allocateWnew();
};


//! \brief A subglacial hydrology model which assumes water pressure is the overburden pressure.
/*!
This model conserves water and transports it in the map-plane.  It was promised
as a PISM addition in in Bueler's talk at IGS 2012 Fairbanks:
  http://www2.gi.alaska.edu/snowice/glaciers/iceflow/bueler-igs-fairbanks-june2012.pdf

This is (essentially) the minimal hydrology model that has lateral motion of
subglacial water.  In such models the water velocity is along the steepest
descent route for the hydraulic potential.  In the particular simplified case
here this potential is (mostly) a function of ice sheet geometry, because the
water pressure is (a multiple of) the overburden pressure.  However, the water
layer thickness is also a part of the hydraulic potential because it is the
potential of the \e top of the aquifer.

This (essential) model has been used for finding locations of subglacial lakes
[\ref Siegertetal2009].  Subglacial lakes will occur at local minima of the
hydraulic potential.  Thus if water builds up significantly (e.g. 10s of meters
or more) then in the specific model here the resulting lakes diffuse instead of
becoming infinitely deep.  Thus we avoid delta functions at the minima of the
hydraulic potential.  This specific model is a well-posed PDE which finds
subglacial lakes.

This model should generally be tested using static ice geometry first, i.e. using
option -no_mass.  Use option `-report_mass_accounting` to see stdout reports
which balance the books on this model.

As with PISMTillCanHydrology and PISMDiffuseOnlyHydrology, the state space
includes only the water layer thickness W.  For more complete modeling where the
water pressure is determined by a physical model for the opening and closing of
cavities, and where the state space is both W and P, use PISMDistributedHydrology.

Note there is an option `-hydrology_null_strip` `X` which produces a strip of
`X` km around the edge of the computational domain in which the water flow
velocity is set to zero.  The water amount is also reset to zero at the end
of each time step in this strip in an accounted way.

As noted this is the minimal model which has a lateral water flux.  This flux is
    \f[ \mathbf{q} = - K \nabla \psi = \mathbf{V} W - D \nabla W \f]
where \f$\psi\f$ is the hydraulic potential
    \f[ \psi = P + \rho_w g (b + W). \f]
The generalized conductivity \f$K\f$ is nontrivial; see
PISMRoutingHydrology::conductivity_staggered().

Unlike the general case PISMHydrology, this model contains the fields which
allow us to compute the wall melt generated by dissipating the gravitational
potential energy in the subglacial water as heat, thus melt, on the
cavity/conduit walls.  In particular we have flux and potential so the water
input in the mass continuity equation can have the added term
    \f[ m_{wall} = - \mathbf{q} \cdot \nabla \psi \qquad \text{FIXME: check} \f]
But this usage is optional ... FIXME: explain
 */
class PISMRoutingHydrology : public PISMHydrology {
public:
  PISMRoutingHydrology(IceGrid &g, const NCConfigVariable &conf);
  virtual ~PISMRoutingHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual void add_vars_to_output(string keyword, set<string> &result);
  virtual PetscErrorCode define_variables(set<string> vars, const PIO &nc,PISM_IO_Type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, const PIO &nc);

  virtual void get_diagnostics(map<string, PISMDiagnostic*> &dict);

  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt);

  virtual PetscErrorCode subglacial_water_thickness(IceModelVec2S &result);
  virtual PetscErrorCode subglacial_water_pressure(IceModelVec2S &result);
  virtual PetscErrorCode subglacial_hydraulic_potential(IceModelVec2S &result);
  virtual PetscErrorCode wall_melt(IceModelVec2S &result);

  virtual PetscErrorCode velocity_staggered(IceModelVec2Stag &result);

protected:
  // this model's state
  IceModelVec2S W;      // water layer thickness
  // this model's auxiliary variables
  IceModelVec2Stag V,   // components are
                        //   V(i,j,0) = u(i,j) = east-edge  centered x-component of water velocity
                        //   V(i,j,1) = v(i,j) = north-edge centered y-component of water velocity
                   Wstag,// edge-centered (staggered) W values (averaged from regular)
                   Kstag,// edge-centered (staggered) values of nonlinear conductivity
                   Qstag;// edge-centered (staggered) advection fluxes
  // this model's workspace variables
  IceModelVec2S Wnew, Pover, R;

  PetscReal stripwidth; // width in m of strip around margin where V and W are set to zero;
                        // if negative then the strip mechanism is inactive inactive

  virtual PetscErrorCode allocate();
  virtual PetscErrorCode init_actions(PISMVars &vars, bool i_set, bool bootstrap_set);

  virtual PetscErrorCode boundary_mass_changes_with_null(IceModelVec2S &Wnew,
             PetscReal &icefreelost, PetscReal &oceanlost,
             PetscReal &negativegain, PetscReal &nullstriplost);

  virtual PetscErrorCode check_W_nonnegative();
  virtual PetscErrorCode water_thickness_staggered(IceModelVec2Stag &result);

  virtual PetscErrorCode conductivity_staggered(IceModelVec2Stag &result, PetscReal &maxKW);
  virtual PetscErrorCode advective_fluxes(IceModelVec2Stag &result);

  virtual PetscErrorCode adaptive_for_W_evolution(
            PetscReal t_current, PetscReal t_end, PetscReal maxKW,
            PetscReal &dt_result,
            PetscReal &maxV_result, PetscReal &maxD_result,
            PetscReal &dtCFL_result, PetscReal &dtDIFFW_result);

  PetscErrorCode raw_update_W(PetscReal hdt);

  inline bool in_null_strip(PetscInt i, PetscInt j) {
    if (stripwidth < 0.0) return false;
    return ((grid.x[i] <= grid.x[0] + stripwidth) || (grid.x[i] >= grid.x[grid.Mx-1] - stripwidth)
            || (grid.y[j] <= grid.y[0] + stripwidth) || (grid.y[j] >= grid.y[grid.My-1] - stripwidth));
  }
};


//! \brief The PISM subglacial hydrology model for a distributed linked-cavity system.
/*!
This implements the new van Pelt & Bueler model documented at the repo (currently
private):
  https://github.com/bueler/hydrolakes
Unlike PISMRoutingHydrology, the water pressure P is a state variable, and there
are modeled mechanisms for cavity geometry evolution, including creep closure
and opening through sliding ("cavitation").  Because of cavitation, this model
needs access to a PISMStressBalance object.

In addition to the actions within the null strip taken by PISMRoutingHydrology,
this model also sets the staggered grid values of the gradient of the hydraulic
potential to zero if either regular grid neighbor is in the null strip.
 */
class PISMDistributedHydrology : public PISMRoutingHydrology {
public:
  PISMDistributedHydrology(IceGrid &g, const NCConfigVariable &conf, PISMStressBalance *sb);
  virtual ~PISMDistributedHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual void add_vars_to_output(string keyword, set<string> &result);
  virtual void get_diagnostics(map<string, PISMDiagnostic*> &dict);
  virtual PetscErrorCode define_variables(set<string> vars, const PIO &nc,PISM_IO_Type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, const PIO &nc);

  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt);

  virtual PetscErrorCode subglacial_water_pressure(IceModelVec2S &result);
  virtual PetscErrorCode englacial_water_thickness(IceModelVec2S &result);

protected:
  // this model's state, in addition to what is in PISMRoutingHydrology
  IceModelVec2S Wen,    // englacial water thickness
                P;      // water pressure
  // this model's auxiliary variables, in addition ...
  IceModelVec2S psi,    // hydraulic potential
                cbase,  // sliding speed of overlying ice
                Pnew;   // pressure during update

  // need to get basal sliding velocity (thus speed):
  PISMStressBalance* stressbalance;

  virtual PetscErrorCode allocate_englacial();
  virtual PetscErrorCode allocate_pressure();

  virtual PetscErrorCode check_Wen_nonnegative();
  virtual PetscErrorCode check_P_bounds(bool enforce_upper);

  virtual PetscErrorCode update_cbase(IceModelVec2S &result);
  virtual PetscErrorCode P_from_W_steady(IceModelVec2S &result);

  virtual PetscErrorCode adaptive_for_WandP_evolution(
                           PetscReal t_current, PetscReal t_end, PetscReal maxKW,
                           PetscReal &dt_result,
                           PetscReal &maxV_result, PetscReal &maxD_result,
                           PetscReal &PtoCFLratio);

  virtual PetscErrorCode update_englacial_storage(
                               IceModelVec2S &myPnew, IceModelVec2S &Wnew_tot);
};

#endif /* _PISMHYDROLOGY_H_ */

