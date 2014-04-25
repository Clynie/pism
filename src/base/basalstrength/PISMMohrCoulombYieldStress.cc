// Copyright (C) 2004--2014 PISM Authors
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

#include "PISMMohrCoulombYieldStress.hh"
#include "PISMHydrology.hh"
#include "PISMVars.hh"
#include "pism_options.hh"
#include "Mask.hh"
#include <cmath>

namespace pism {

//! \file PISMMohrCoulombYieldStress.cc  Process model which computes pseudo-plastic yield stress for the subglacial layer.
/*! \file PISMMohrCoulombYieldStress.cc
The output variable of this submodel is `tauc`, the pseudo-plastic yield stress
field that is used in the ShallowStressBalance objects.  This quantity is
computed by the Mohr-Coulomb criterion [\ref SchoofTill], but using an empirical
relation between the amount of water in the till and the effective pressure
of the overlying glacier resting on the till [\ref Tulaczyketal2000].

The "dry" strength of the till is a state variable which is private to
the submodel, namely `tillphi`.  Its initialization is nontrivial: either the
`-topg_to_phi`  heuristic is used or inverse modeling can be used.  (In the
latter case `tillphi` can be read-in at the beginning of the run.  Currently
`tillphi` does not evolve during the run.)

This submodel uses a pointer to a Hydrology instance to get the till (pore)
water amount, the effective water layer thickness.  The effective pressure is
derived from this.  Then the effective pressure is combined with tillphi to
compute an updated `tauc` by the Mohr-Coulomb criterion.

This submodel is inactive in floating areas.
*/


MohrCoulombYieldStress::MohrCoulombYieldStress(IceGrid &g,
                                                       const Config &conf,
                                                       Hydrology *hydro)
  : YieldStress(g, conf) {
  m_bed_topography = NULL;
  m_mask           = NULL;
  m_hydrology      = hydro;

  if (allocate() != 0) {
    PetscPrintf(grid.com,
                "PISM ERROR: memory allocation failed in YieldStress constructor.\n");
    PISMEnd();
  }
}

MohrCoulombYieldStress::~MohrCoulombYieldStress() {
  // empty
}


PetscErrorCode MohrCoulombYieldStress::allocate() {
  PetscErrorCode ierr;

  ierr = m_till_phi.create(grid, "tillphi", WITH_GHOSTS, grid.max_stencil_width); CHKERRQ(ierr);
  ierr = m_till_phi.set_attrs("model_state",
                              "friction angle for till under grounded ice sheet",
                              "degrees", ""); CHKERRQ(ierr);
  m_till_phi.set_time_independent(true);
  // in this model; need not be time-independent in general

  ierr = m_tauc.create(grid, "tauc", WITH_GHOSTS, grid.max_stencil_width); CHKERRQ(ierr);
  ierr = m_tauc.set_attrs("diagnostic",
                          "yield stress for basal till (plastic or pseudo-plastic model)",
                          "Pa", ""); CHKERRQ(ierr);

  // internal working space; stencil width needed because redundant computation
  // on overlaps
  ierr = m_tillwat.create(grid, "tillwat_for_MohrCoulomb",
                          WITH_GHOSTS, grid.max_stencil_width); CHKERRQ(ierr);
  ierr = m_tillwat.set_attrs("internal",
                             "copy of till water thickness held by MohrCoulombYieldStress",
                             "m", ""); CHKERRQ(ierr);
  bool addtransportable = config.get_flag("tauc_add_transportable_water");
  if (addtransportable == true) {
    ierr = m_bwat.create(grid, "bwat_for_MohrCoulomb", WITHOUT_GHOSTS); CHKERRQ(ierr);
    ierr = m_bwat.set_attrs("internal",
                            "copy of transportable water thickness held by MohrCoulombYieldStress",
                            "m", ""); CHKERRQ(ierr);
  }
  ierr = m_Po.create(grid, "overburden_pressure_for_MohrCoulomb",
                     WITH_GHOSTS, grid.max_stencil_width); CHKERRQ(ierr);
  ierr = m_Po.set_attrs("internal",
                        "copy of overburden pressure held by MohrCoulombYieldStress",
                        "Pa", ""); CHKERRQ(ierr);

  return 0;
}


//! Initialize the pseudo-plastic till mechanical model.
/*!
The pseudo-plastic till basal resistance model is governed by this power law
equation,
    \f[ \tau_b = - \frac{\tau_c}{|\mathbf{U}|^{1-q} U_{\mathtt{th}}^q} \mathbf{U}, \f]
where \f$\tau_b=(\tau_{(b)x},\tau_{(b)y})\f$ is the basal shear stress and
\f$U=(u,v)\f$ is the sliding velocity.

We call the scalar field \f$\tau_c(t,x,y)\f$ the \e yield \e stress even when
the power \f$q\f$ is not zero; when that power is zero the formula describes
a plastic material with an actual yield stress.  The constant
\f$U_{\mathtt{th}}\f$ is the \e threshhold \e speed, and \f$q\f$ is the \e pseudo
\e plasticity \e exponent.  The current class computes this yield stress field.
See also IceBasalResistancePlasticLaw::drag().

The strength of the saturated till material, the yield stress, is modeled by a
Mohr-Coulomb relation [\ref Paterson, \ref SchoofStream],
    \f[   \tau_c = c_0 + (\tan \varphi) N_{til}, \f]
where \f$N_{til}\f$ is the effective pressure of the glacier on the mineral
till.

The determination of the till friction angle \f$\varphi(x,y)\f$  is important.
It is assumed in this default model to be a time-independent factor which
describes the strength of the unsaturated "dry" (mineral) till material.  Thus
it is assumed to change more slowly than the till water pressure, and it follows
that it changes more slowly than the yield stress and the basal shear stress.

Option `-topg_to_phi` causes call to topg_to_phi() at the beginning of the run.
This determines the map of \f$\varphi(x,y)\f$.  If this option is note given,
the current method leaves `tillphi` unchanged, and thus either in its
read-in-from-file state or with a default constant value from the config file.
*/
PetscErrorCode MohrCoulombYieldStress::init(Vars &vars)
{
  PetscErrorCode ierr;
  bool topg_to_phi_set, plastic_phi_set, bootstrap, i_set,
    tauc_to_phi_set;
  std::string filename;
  int start;

  m_variables = &vars;

  {
    std::string hydrology_tillwat_max = "hydrology_tillwat_max";
    bool till_is_present = config.get(hydrology_tillwat_max) > 0.0;

    if (till_is_present == false) {
      PetscPrintf(grid.com,
                  "PISM ERROR: The Mohr-Coulomb yield stress model cannot be used without till.\n"
                  "            Reset %s or choose a different yield stress model.\n",
                  hydrology_tillwat_max.c_str());
      PISMEnd();
    }
  }

  {
    const std::string flag_name = "tauc_add_transportable_water";
    RoutingHydrology *hydrology_routing = dynamic_cast<RoutingHydrology*>(m_hydrology);
    if (config.get_flag(flag_name) == true && hydrology_routing == NULL) {
      PetscPrintf(grid.com,
                  "PISM ERROR: Flag %s is set.\n"
                  "            Thus the Mohr-Coulomb yield stress model needs a RoutingHydrology\n"
                  "            (or derived like DistributedHydrology) object with transportable water.\n"
                  "            The current Hydrology instance is not suitable.  Set flag\n"
                  "            %s to 'no' or choose a different yield stress model.\n",
                  flag_name.c_str(), flag_name.c_str());
      PISMEnd();
    }
  }

  ierr = verbPrintf(2, grid.com, "* Initializing the default basal yield stress model...\n"); CHKERRQ(ierr);

  m_bed_topography = dynamic_cast<IceModelVec2S*>(vars.get("bedrock_altitude"));
  if (m_bed_topography == NULL) SETERRQ(grid.com, 1, "bedrock_altitude is not available");

  m_mask = dynamic_cast<IceModelVec2Int*>(vars.get("mask"));
  if (m_mask == NULL) SETERRQ(grid.com, 1, "mask is not available");

  ierr = PetscOptionsBegin(grid.com, "", "Options controlling the basal till yield stress model", ""); CHKERRQ(ierr);
  {
    ierr = OptionsIsSet("-plastic_phi", plastic_phi_set); CHKERRQ(ierr);
    ierr = OptionsIsSet("-topg_to_phi",
                            "Use the till friction angle parameterization", topg_to_phi_set); CHKERRQ(ierr);
    ierr = OptionsIsSet("-i", "PISM input file", i_set); CHKERRQ(ierr);
    ierr = OptionsIsSet("-boot_file", "PISM bootstrapping file",
                            bootstrap); CHKERRQ(ierr);
    ierr = OptionsIsSet("-tauc_to_phi", "Compute tillphi as a function of tauc and the rest of the model state",
                            tauc_to_phi_set); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  // Get the till friction angle from the the context and ignore options that
  // would be used to set it otherwise.
  IceModelVec2S *till_phi_input = dynamic_cast<IceModelVec2S*>(vars.get("tillphi"));
  if (till_phi_input != NULL) {
    ierr = m_till_phi.copy_from(*till_phi_input); CHKERRQ(ierr);

    ierr = ignore_option(grid.com, "-plastic_phi"); CHKERRQ(ierr);
    ierr = ignore_option(grid.com, "-topg_to_phi"); CHKERRQ(ierr);

    // We do not allow re-gridding in this case.

    return 0;
  }

  if (topg_to_phi_set && plastic_phi_set) {
    PetscPrintf(grid.com, "ERROR: only one of -plastic_phi and -topg_to_phi is allowed.\n");
    PISMEnd();
  }

  if (plastic_phi_set) {

    ierr = m_till_phi.set(config.get("default_till_phi")); CHKERRQ(ierr);

  } else if (topg_to_phi_set) {

    ierr = verbPrintf(2, grid.com,
                      "  option -topg_to_phi seen; creating tillphi map from bed elev ...\n");
    CHKERRQ(ierr);

    if (i_set || bootstrap) {
      ierr = find_pism_input(filename, bootstrap, start); CHKERRQ(ierr);

      PIO nc(grid.com, "guess_mode", grid.get_unit_system());
      bool tillphi_present;

      ierr = nc.open(filename, PISM_READONLY); CHKERRQ(ierr);
      ierr = nc.inq_var(m_till_phi.metadata().get_string("short_name"), tillphi_present); CHKERRQ(ierr);
      ierr = nc.close(); CHKERRQ(ierr);

      if (tillphi_present) {
        ierr = verbPrintf(2, grid.com,
                          "PISM WARNING: -topg_to_phi computation will override the '%s' field\n"
                          "              present in the input file '%s'!\n",
                          m_till_phi.metadata().get_string("short_name").c_str(), filename.c_str()); CHKERRQ(ierr);
      }
    }

    // note option -topg_to_phi will be read again to get comma separated array of parameters
    ierr = topg_to_phi(); CHKERRQ(ierr);

  } else if (i_set || bootstrap) {

    ierr = find_pism_input(filename, bootstrap, start); CHKERRQ(ierr);

    if (i_set) {
      ierr = m_till_phi.read(filename, start); CHKERRQ(ierr);
    } else {
      ierr = m_till_phi.regrid(filename, OPTIONAL,
                                 config.get("bootstrapping_tillphi_value_no_var")); CHKERRQ(ierr);
    }
  }

  // regrid if requested, regardless of how initialized
  ierr = regrid("MohrCoulombYieldStress", &m_till_phi); CHKERRQ(ierr);

  if (tauc_to_phi_set) {
    std::string tauc_to_phi_file;
    bool flag;
    ierr = OptionsString("-tauc_to_phi", "Specifies the file tauc will be read from",
                             tauc_to_phi_file, flag, true); CHKERRQ(ierr);

    if (tauc_to_phi_file.empty() == false) {
      // "-tauc_to_phi filename.nc" is given
      ierr = m_tauc.regrid(tauc_to_phi_file, CRITICAL); CHKERRQ(ierr);
    } else {
      // "-tauc_to_phi" is given (without a file name); assume that tauc has to
      // be present in an input file
      ierr = find_pism_input(filename, bootstrap, start); CHKERRQ(ierr);

      if (bootstrap == false) {
        ierr = m_tauc.read(filename, start); CHKERRQ(ierr);
      } else {
        ierr = m_tauc.regrid(filename, CRITICAL); CHKERRQ(ierr);
      }
    }

    // At this point tauc is initialized in one of the following ways:
    // - from a -tauc_to_phi file
    // - from an input (or bootstrapping) file if -tauc_to_phi did not have an
    //  argument
    //
    // In addition to this, till_phi is initialized
    // - from an input file or
    // - using the -plastic_phi option
    // - using the topg_to_phi option
    //
    // Now tauc_to_phi() will correct till_phi at all locations where grounded
    // ice is present:

    ierr = verbPrintf(2, grid.com, "  Computing till friction angle (tillphi) as a function of the yield stress (tauc)...\n"); 
    CHKERRQ(ierr);

    ierr = tauc_to_phi(); CHKERRQ(ierr);

  } else {
    ierr = m_tauc.set(0.0); CHKERRQ(ierr);
  }

  // ensure that update() computes tauc at the beginning of the run:
  m_t = m_dt = GSL_NAN;

  return 0;
}


void MohrCoulombYieldStress::add_vars_to_output(std::string /*keyword*/, std::set<std::string> &result) {
  result.insert("tillphi");
}


PetscErrorCode MohrCoulombYieldStress::define_variables(std::set<std::string> vars, const PIO &nc,
                                                 IO_Type nctype) {
  if (set_contains(vars, "tillphi")) {
    PetscErrorCode ierr = m_till_phi.define(nc, nctype); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode MohrCoulombYieldStress::write_variables(std::set<std::string> vars, const PIO &nc) {
  if (set_contains(vars, "tillphi")) {
    PetscErrorCode ierr = m_till_phi.write(nc); CHKERRQ(ierr);
  }
  return 0;
}


//! Update the till yield stress for use in the pseudo-plastic till basal stress
//! model.  See also IceBasalResistancePlasticLaw.
/*!
Updates yield stress  @f$ \tau_c @f$  based on modeled till water layer thickness
from a Hydrology object.  We implement the Mohr-Coulomb criterion allowing
a (typically small) till cohesion  @f$ c_0 @f$ 
and by expressing the coefficient as the tangent of a till friction angle
 @f$ \varphi @f$ :
    @f[   \tau_c = c_0 + (\tan \varphi) N_{til}. @f]
See [@ref Paterson] table 8.1 regarding values.

The effective pressure on the till is empirically-related
to the amount of water in the till, namely this formula derived from
[@ref Tulaczyketal2000]:

@f[ N_til = \delta P_o 10^{(e_0/C_c) (1 - W_{til}/W_{til}^{max})} @f]

where  @f$ \delta @f$ =`till_effective_fraction_overburden`,  @f$ P_o @f$  is the
overburden pressure,  @f$ e_0 @f$ =`till_reference_void_ratio` is the void ratio
at the effective pressure minimum, and  @f$ C_c @f$ ==`till_compressibility_coefficient`
is the coefficient of compressibility of the till.  Constants  @f$ e_0,C_c @f$  are
derived by [@ref Tulaczyketal2000] from laboratory experiments on samples of
till.  Also  @f$ W_{til}^{max} @f$ =`hydrology_tillwat_max`.

If `tauc_add_transportable_water` is yes then the above formula becomes

@f[ N_til = \delta P_o 10^{(e_0/C_c) (1 - (W+W_{til})/W_{til}^{max})}, @f]

that is, here the water amount is the sum @f$ W+W_{til} @f$.  This only works
if @f$ W @f$ is present, that is, if `hydrology` points to a
RoutingHydrology (or derived class thereof).
 */
PetscErrorCode MohrCoulombYieldStress::update(double my_t, double my_dt) {
  PetscErrorCode ierr;

  if ((fabs(my_t - m_t) < 1e-12) &&
      (fabs(my_dt - m_dt) < 1e-12))
    return 0;

  m_t = my_t; m_dt = my_dt;
  // this model does no internal time-stepping

  bool slipperygl       = config.get_flag("tauc_slippery_grounding_lines"),
       addtransportable = config.get_flag("tauc_add_transportable_water");

  const double high_tauc   = config.get("high_tauc"),
               tillwat_max = config.get("hydrology_tillwat_max"),
               c0          = config.get("till_c_0"),
               e0overCc    = config.get("till_reference_void_ratio")
                                / config.get("till_compressibility_coefficient"),
               delta       = config.get("till_effective_fraction_overburden"),
               tlftw       = config.get("till_log_factor_transportable_water");


  RoutingHydrology* hydrowithtransport = dynamic_cast<RoutingHydrology*>(m_hydrology);
  if (m_hydrology) {
    ierr = m_hydrology->till_water_thickness(m_tillwat); CHKERRQ(ierr);
    ierr = m_hydrology->overburden_pressure(m_Po); CHKERRQ(ierr);
    if (addtransportable == true) {
        assert(hydrowithtransport != NULL);
        ierr = hydrowithtransport->subglacial_water_thickness(m_bwat); CHKERRQ(ierr);
    }
  }

  if (addtransportable == true) {
    ierr = m_bwat.begin_access(); CHKERRQ(ierr);
  }
  ierr = m_tillwat.begin_access();         CHKERRQ(ierr);
  ierr = m_till_phi.begin_access();        CHKERRQ(ierr);
  ierr = m_tauc.begin_access();            CHKERRQ(ierr);
  ierr = m_mask->begin_access();           CHKERRQ(ierr);
  ierr = m_bed_topography->begin_access(); CHKERRQ(ierr);
  ierr = m_Po.begin_access();              CHKERRQ(ierr);
  MaskQuery m(*m_mask);

  for (int   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (int j = grid.ys; j < grid.ys+grid.ym; ++j) {
      if (m.ocean(i, j)) {
        m_tauc(i, j) = 0.0;
      } else if (m.ice_free(i, j)) {
        m_tauc(i, j) = high_tauc;  // large yield stress if grounded and ice-free
      } else { // grounded and there is some ice
        // user can ask that marine grounding lines get special treatment
        const double sea_level = 0.0; // FIXME: get sea-level from correct PISM source

        double water = m_tillwat(i,j); // usual case
        if (slipperygl == true &&
            (*m_bed_topography)(i,j) <= sea_level &&
            (m.next_to_floating_ice(i,j) || m.next_to_ice_free_ocean(i,j)) ) {
          water = tillwat_max;
        } else if (addtransportable == true) {
          water = m_tillwat(i,j) + tlftw * log(1.0 + m_bwat(i,j) / tlftw);
        }

        double Ntil = delta * m_Po(i,j) * pow(10.0, e0overCc * (1.0 - (water / tillwat_max)));
        Ntil = PetscMin(m_Po(i,j), Ntil);
        m_tauc(i, j) = c0 + Ntil * tan((M_PI/180.0) * m_till_phi(i, j));
      }
    }
  }
  ierr = m_Po.end_access();              CHKERRQ(ierr);
  ierr = m_bed_topography->end_access(); CHKERRQ(ierr);
  ierr = m_mask->end_access();           CHKERRQ(ierr);
  ierr = m_tauc.end_access();            CHKERRQ(ierr);
  ierr = m_till_phi.end_access();        CHKERRQ(ierr);
  ierr = m_tillwat.end_access();         CHKERRQ(ierr);
  if (addtransportable == true) {
    ierr = m_bwat.end_access(); CHKERRQ(ierr);
  }

  ierr = m_tauc.update_ghosts(); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode MohrCoulombYieldStress::basal_material_yield_stress(IceModelVec2S &result) {
  return m_tauc.copy_to(result);
}


//! Computes the till friction angle phi as a piecewise linear function of bed elevation, according to user options.
/*!
Computes the till friction angle \f$\phi(x,y)\f$ at a location, namely
`IceModel::vtillphi`, as the following increasing, piecewise-linear function of
the bed elevation \f$b(x,y)\f$.  Let
        \f[ M = (\phi_{\text{max}} - \phi_{\text{min}}) / (b_{\text{max}} - b_{\text{min}}) \f]
be the slope of the nontrivial part.  Then
        \f[ \phi(x,y) = \begin{cases}
                \phi_{\text{min}}, & b(x,y) \le b_{\text{min}}, \\
                \phi_{\text{min}} + (b(x,y) - b_{\text{min}}) \,M,
                                  &  b_{\text{min}} < b(x,y) < b_{\text{max}}, \\
                \phi_{\text{max}}, & b_{\text{max}} \le b(x,y), \end{cases} \f]
The exception is if the point is marked as floating, in which case the till friction angle
is set to the value `phi_ocean`.

The default values are vaguely suitable for Antarctica, perhaps:
- `phi_min` = 5.0 degrees,
- `phi_max` = 15.0 degrees,
- `topg_min` = -1000.0 m,
- `topg_max` = 1000.0 m,
- `phi_ocean` = 10.0 degrees.

If the user gives option <code>-topg_to_phi A,B,C,D</code> then `phi_ocean`
is not used. Instead, the same rule as above for grounded ice is used.
 */
PetscErrorCode MohrCoulombYieldStress::topg_to_phi() {
  PetscErrorCode ierr;
  bool  topg_to_phi_set;
  std::vector<double> inarray(4);

  // default values:
  inarray[0] = 5.0;
  inarray[1] = 15.0;
  inarray[2] = -1000.0;
  inarray[3] = 1000.0;

  // read the comma-separated list of four values
  ierr = OptionsRealArray("-topg_to_phi", "phi_min, phi_max, topg_min, topg_max",
                              inarray, topg_to_phi_set); CHKERRQ(ierr);

  assert(topg_to_phi_set == true);

  if (inarray.size() != 4) {
    PetscPrintf(grid.com,
                "PISM ERROR: option -topg_to_phi requires a comma-separated list with 4 numbers; got %d\n",
                inarray.size());
    PISMEnd();
  }

  double phi_min = inarray[0], phi_max = inarray[1],
    topg_min = inarray[2], topg_max = inarray[3];

  if (phi_min >= phi_max) {
    PetscPrintf(grid.com,
                "PISM ERROR: invalid -topg_to_phi arguments: phi_min < phi_max is required\n");
    PISMEnd();
  }

  if (topg_min >= topg_max) {
    PetscPrintf(grid.com,
                "PISM ERROR: invalid -topg_to_phi arguments: topg_min < topg_max is required\n");
    PISMEnd();
  }

  ierr = verbPrintf(2, grid.com,
                    "  till friction angle (phi) is piecewise-linear function of bed elev (topg):\n"
                    "            /  %5.2f                                 for   topg < %.f\n"
                    "      phi = |  %5.2f + (topg - (%.f)) * (%.2f / %.f)   for   %.f < topg < %.f\n"
                    "            \\  %5.2f                                 for   %.f < topg\n",
                    phi_min, topg_min,
                    phi_min, topg_min, phi_max - phi_min, topg_max - topg_min, topg_min, topg_max,
                    phi_max, topg_max); CHKERRQ(ierr);

  double slope = (phi_max - phi_min) / (topg_max - topg_min);

  ierr = m_bed_topography->begin_access(); CHKERRQ(ierr);
  ierr = m_till_phi.begin_access(); CHKERRQ(ierr);

  for (int i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (int j = grid.ys; j < grid.ys + grid.ym; ++j) {
      double bed = (*m_bed_topography)(i, j);

      if (bed <= topg_min) {
        m_till_phi(i, j) = phi_min;
      } else if (bed >= topg_max) {
        m_till_phi(i, j) = phi_max;
      } else {
        m_till_phi(i, j) = phi_min + (bed - topg_min) * slope;
      }

    }
  }

  ierr = m_bed_topography->end_access(); CHKERRQ(ierr);
  ierr = m_till_phi.end_access(); CHKERRQ(ierr);

  // communicate ghosts so that the tauc computation can be performed locally
  // (including ghosts of tauc, that is)
  ierr = m_till_phi.update_ghosts(); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode MohrCoulombYieldStress::tauc_to_phi() {
  PetscErrorCode ierr;
  const double c0 = config.get("till_c_0"),
    e0overCc      = config.get("till_reference_void_ratio")/ config.get("till_compressibility_coefficient"),
    delta         = config.get("till_effective_fraction_overburden"),
    tillwat_max   = config.get("hydrology_tillwat_max");

  assert(m_hydrology != NULL);

  ierr = m_hydrology->till_water_thickness(m_tillwat); CHKERRQ(ierr);
  ierr = m_hydrology->overburden_pressure(m_Po); CHKERRQ(ierr);

  ierr = m_mask->begin_access();    CHKERRQ(ierr);
  ierr = m_tauc.begin_access();     CHKERRQ(ierr);
  ierr = m_tillwat.begin_access();  CHKERRQ(ierr);
  ierr = m_Po.begin_access();       CHKERRQ(ierr);
  ierr = m_till_phi.begin_access(); CHKERRQ(ierr);
  MaskQuery m(*m_mask);
  int GHOSTS = grid.max_stencil_width;
  for (int   i = grid.xs - GHOSTS; i < grid.xs+grid.xm + GHOSTS; ++i) {
    for (int j = grid.ys - GHOSTS; j < grid.ys+grid.ym + GHOSTS; ++j) {
      if (m.ocean(i, j)) {
        // no change
      } else if (m.ice_free(i, j)) {
        // no change
      } else { // grounded and there is some ice
        double Ntil = delta * m_Po(i,j) * pow(10.0, e0overCc * (1.0 - (m_tillwat(i,j) / tillwat_max)));
        m_till_phi(i, j) = 180.0/M_PI * atan((m_tauc(i, j) - c0) / Ntil);
      }
    }
  }
  ierr = m_mask->end_access();    CHKERRQ(ierr);
  ierr = m_tauc.end_access();     CHKERRQ(ierr);
  ierr = m_till_phi.end_access(); CHKERRQ(ierr);
  ierr = m_tillwat.end_access();  CHKERRQ(ierr);
  ierr = m_Po.end_access();       CHKERRQ(ierr);

  return 0;
}

} // end of namespace pism
