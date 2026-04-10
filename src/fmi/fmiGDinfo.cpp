/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
Copyright (C) 2012 Modelon AB

This program is free software: you can redistribute it and/or modify
it under the terms of the BSD style license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
FMILIB_License.txt file for more details.

You should have received a copy of the FMILIB_License.txt file
along with this program. If not, contact Modelon AB <http://www.modelon.com>.
*/
#include "fmiGDinfo.h"

#include "core/objectFactoryTemplates.hpp"
#include "fmi_importGD.h"
#include "fmi_models/CymeDistLoad.h"
#include "fmi_models/fmiCoSimLoad.h"
#include "fmi_models/fmiCoSimLoad3phase.h"
#include "fmi_models/fmiExciter.h"
#include "fmi_models/fmiGenModel.h"
#include "fmi_models/fmiGovernor.h"
#include "fmi_models/fmiMELoad.h"
#include "fmi_models/fmiMELoad3phase.h"
#include "plugins/gridDynPluginApi.h"
#include <memory>
#include <string>
#include <vector>

#include <boost/dll/alias.hpp>  // for BOOST_DLL_ALIAS

// create the component factories

namespace griddyn {
static childTypeFactory<fmi::fmiMELoad, Load> fmild("load", stringVec{"fmimeload", "fmi", "me"});
static childTypeFactory<fmi::fmiCoSimLoad, Load> fmiCSld("load",
                                                         stringVec{"fmicosimload", "cosim"});
static childTypeFactory<fmi::fmiCoSimLoad3phase, Load>
    fmiCSld3("load", stringVec{"fmicosimload3", "fmicosimload3phase"});
static childTypeFactory<fmi::fmiMELoad3phase, Load> fmiMEld3(
    "load",
    stringVec{"fmimeload3", "fmiload3phase", "fmi3phase", "fmimeload3phase", "fmime3phase"});
static childTypeFactory<fmi::fmiGovernor, Governor>
    fmiGov("governor", stringVec{"fmigov", "fmigovernor", "fmi"});
static childTypeFactory<fmi::fmiExciter, Exciter> fmiExciter("exciter",
                                                             stringVec{"fmiexiter", "fmi"});
static childTypeFactory<fmi::fmiGenModel, GenModel>
    fmiGM("genmodel", stringVec{"fmigenmodel", "fmimachine", "fmi"});
static childTypeFactory<fmi::CymeDistLoadME, Load> cymeME("load",
                                                          stringVec{"cyme", "cymeme", "cymefmi"});

void loadFmiLibrary()
{
    static int loaded = 0;

    if (loaded == 0) {
        loaded = 1;
    }
}

}  // namespace griddyn

// Someday I will get plugins to work
namespace fmi_plugin_namespace {
class fmiPlugin: public gridDynPlugInApi {
    static std::vector<std::shared_ptr<griddyn::objectFactory>> fmiFactories;
    fmiPlugin() = default;

  public:
    std::string name() const override { return "fmi"; }

    void load() override
    {
        auto b =
            std::make_shared<griddyn::childTypeFactory<griddyn::fmi::fmiMELoad, griddyn::Load>>(
                "load",
                griddyn::stringVec{"fmiload", "fmi"});
        fmiFactories.push_back(b);
    }

    void load(const std::string& /*section*/) override { load(); }
    // Factory method
    static std::shared_ptr<fmiPlugin> create()
    {
        return std::shared_ptr<fmiPlugin>(new fmiPlugin());
    }
};

std::vector<std::shared_ptr<griddyn::objectFactory>> fmiPlugin::fmiFactories;

/*BOOST_DLL_ALIAS(
    fmi_plugin_namespace::fmiPlugin::create, // <-- this function is exported with...
    load_plugin                               // <-- ...this alias name
)*/

}  // namespace fmi_plugin_namespace
