/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
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
static childTypeFactory<fmi::FmiMELoad, Load>
    fmild(  // NOLINT(bugprone-throwing-static-initialization)
        "load", std::to_array<std::string_view>({"fmimeload", "fmi", "me"}));
static childTypeFactory<fmi::FmiCoSimLoad, Load>
    fmiCSld(  // NOLINT(bugprone-throwing-static-initialization)
        "load", std::to_array<std::string_view>({"fmicosimload", "cosim"}));
static childTypeFactory<fmi::FmiCoSimLoad3phase, Load>
    fmiCSld3(  // NOLINT(bugprone-throwing-static-initialization)
        "load", std::to_array<std::string_view>({"fmicosimload3", "fmicosimload3phase"}));
static childTypeFactory<fmi::FmiMELoad3phase, Load>
    fmiMEld3(  // NOLINT(bugprone-throwing-static-initialization)
        "load",
        std::to_array<std::string_view>(
            {"fmimeload3", "fmiload3phase", "fmi3phase", "fmimeload3phase", "fmime3phase"}));
static childTypeFactory<fmi::FmiGovernor, Governor>
    fmiGov(  // NOLINT(bugprone-throwing-static-initialization)
        "governor", std::to_array<std::string_view>({"fmigov", "fmigovernor", "fmi"}));
static childTypeFactory<fmi::FmiExciter, Exciter>
    fmiExciter(  // NOLINT(bugprone-throwing-static-initialization)
        "exciter", std::to_array<std::string_view>({"fmiexiter", "fmi"}));
static childTypeFactory<fmi::FmiGenModel, GenModel>
    fmiGM(  // NOLINT(bugprone-throwing-static-initialization)
        "genmodel", std::to_array<std::string_view>({"fmigenmodel", "fmimachine", "fmi"}));
static childTypeFactory<fmi::CymeDistLoadME, Load>
    cymeME(  // NOLINT(bugprone-throwing-static-initialization)
        "load", std::to_array<std::string_view>({"cyme", "cymeme", "cymefmi"}));

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
namespace {
class FmiPlugin: public gridDynPlugInApi {
    static std::vector<std::shared_ptr<griddyn::objectFactory>> fmiFactories;
    FmiPlugin() = default;

  public:
    [[nodiscard]] std::string name() const override { return "fmi"; }

    void load() override
    {
        auto factory =
            std::make_shared<griddyn::childTypeFactory<griddyn::fmi::FmiMELoad, griddyn::Load>>(
                "load", griddyn::stringVec{"fmiload", "fmi"});
        fmiFactories.push_back(factory);
    }

    void load(const std::string& /*section*/) override { load(); }
    // Factory method
    static std::shared_ptr<FmiPlugin> create()
    {
        return std::shared_ptr<FmiPlugin>(new FmiPlugin());
    }
};
}  // namespace

std::vector<std::shared_ptr<griddyn::objectFactory>> FmiPlugin::fmiFactories;

/*BOOST_DLL_ALIAS(
    fmi_plugin_namespace::FmiPlugin::create, // <-- this function is exported with...
    load_plugin                               // <-- ...this alias name
)*/

}  // namespace fmi_plugin_namespace
