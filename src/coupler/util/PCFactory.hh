// Copyright (C) 2011, 2013, 2014 PISM Authors
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

#ifndef _PCFACTORY_H_
#define _PCFACTORY_H_

#include "pism_const.hh"
#include "pism_options.hh"

#include "IceGrid.hh"
#include <map>

#ifdef PISM_USE_TR1
#include <tr1/memory>
#else
#include <memory>
#endif

namespace pism {

class Config;

template <class Model, class Modifier>
class PCFactory {
public:

  // virtual base class that allows storing different model creators
  // in the same dictionary
  class ModelCreator {
  public:
    virtual Model* create(IceGrid &g, const Config &conf) = 0;
    virtual ~ModelCreator() {}
  };

  // Creator for a specific model class M.
  template <class M>
  class SpecificModelCreator : public ModelCreator {
  public:
    M* create(IceGrid &g, const Config &conf) {
      return new M(g, conf);
    }
  };

  // virtual base class that allows storing different modifier
  // creators in the same dictionary
  class ModifierCreator {
  public:
    virtual Modifier* create(IceGrid &g, const Config &conf, Model* input) = 0;
    virtual ~ModifierCreator() {}
  };

  // Creator for a specific modifier class M.
  template <class M>
  class SpecificModifierCreator : public ModifierCreator {
  public:
    M* create(IceGrid &g, const Config &conf, Model* input) {
      return new M(g, conf, input);
    }
  };

#ifdef PISM_USE_TR1
  typedef std::tr1::shared_ptr<ModelCreator> ModelCreatorPtr;
  typedef std::tr1::shared_ptr<ModifierCreator> ModifierCreatorPtr;
#else
  typedef std::shared_ptr<ModelCreator> ModelCreatorPtr;
  typedef std::shared_ptr<ModifierCreator> ModifierCreatorPtr;
#endif

  PCFactory<Model,Modifier>(IceGrid &g, const Config &conf)
  : m_grid(g), m_config(conf) {}
  ~PCFactory<Model,Modifier>() {}

  //! Sets the default type name.
  PetscErrorCode set_default(std::string name) {
    if (m_models.find(name) == m_models.end()) {
      SETERRQ1(m_grid.com, 1,"ERROR: type %s is not registered", name.c_str());
    } else {
      m_default_type = name;
    }
    return 0;
  }

  //! Creates a boundary model. Processes command-line options.
  PetscErrorCode create(Model* &result) {
    PetscErrorCode ierr;
    std::vector<std::string> choices;
    std::string model_list, modifier_list, descr;
    bool flag = false;

    // build a list of available models:
    typename std::map<std::string, ModelCreatorPtr >::iterator k;
    k = m_models.begin();
    model_list = "[" + (k++)->first;
    for (; k != m_models.end(); k++) {
      model_list += ", " + k->first;
    }
    model_list += "]";

    // build a list of available modifiers:
    typename std::map<std::string, ModifierCreatorPtr >::iterator p;
    p = m_modifiers.begin();
    modifier_list = "[" + (p++)->first;
    for (; p != m_modifiers.end(); p++) {
      modifier_list += ", " + p->first;
    }
    modifier_list += "]";

    descr =  "Sets up the PISM " + m_option + " model. Available models: " + model_list +
      " Available modifiers: " + modifier_list;

    // Get the command-line option:
    ierr = OptionsStringArray("-" + m_option, descr, m_default_type, choices, flag); CHKERRQ(ierr);

    if (choices.empty()) {
      if (flag == true) {
        PetscPrintf(m_grid.com, "ERROR: option -%s requires an argument.\n", m_option.c_str());
        PISMEnd();
      }
      choices.push_back(m_default_type);
    }

    // the first element has to be an *actual* model (not a modifier), so we
    // create it:
    std::vector<std::string>::iterator j = choices.begin();

    if (m_models.find(*j) == m_models.end()) {
      PetscPrintf(m_grid.com,
                  "ERROR: %s model \"%s\" is not available.\n"
                  "  Available models:    %s\n"
                  "  Available modifiers: %s\n",
                  m_option.c_str(), j->c_str(),
                  model_list.c_str(), modifier_list.c_str());
      PISMEnd();
    }

    result = m_models[*j]->create(m_grid, m_config);

    ++j;

    // process remaining arguments:
    while (j != choices.end()) {
      if (m_modifiers.find(*j) == m_modifiers.end()) {
        PetscPrintf(m_grid.com,
                    "ERROR: %s modifier \"%s\" is not available.\n"
                    "  Available modifiers: %s\n",
                    m_option.c_str(), j->c_str(), modifier_list.c_str());
        PISMEnd();
      }

      result =  m_modifiers[*j]->create(m_grid, m_config, result);

      ++j;
    }

    return 0;
  }

  //! Adds a boundary model to the dictionary.
  template <class M>
  void add_model(std::string name) {
    m_models[name] = ModelCreatorPtr(new SpecificModelCreator<M>);
  }

  template <class M>
  void add_modifier(std::string name) {
    m_modifiers[name] = ModifierCreatorPtr(new SpecificModifierCreator<M>);
  }

  //! Removes a boundary model from the dictionary.
  void remove_model(std::string name) {
    m_models.erase(name);
  }

  void remove_modifier(std::string name) {
    m_modifiers.erase(name);
  }

  //! Clears the dictionary.
  void clear_models() {
    m_models.clear();
  }

  void clear_modifiers() {
    m_modifiers.clear();
  }
protected:
  virtual void add_standard_types() {}
  std::string m_default_type, m_option;
  std::map<std::string, ModelCreatorPtr> m_models;
  std::map<std::string, ModifierCreatorPtr> m_modifiers;
  IceGrid &m_grid;
  const Config &m_config;
};

} // end of namespace pism

#endif /* _PCFACTORY_H_ */
