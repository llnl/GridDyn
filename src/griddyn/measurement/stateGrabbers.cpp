/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../Link.h"
#include "../Relay.h"
#include "../relays/Sensor.h"
#include "Condition.h"
#include "GrabberInterpreter.hpp"
#include "GrabberUnitConversion.hpp"
#include "MeasurementExpression.hpp"
#include "StateGrabber.h"
#include "gmlc/containers/mapOps.hpp"
#include "utilities/MatrixDataScale.hpp"
#include "utilities/MatrixDataTranslate.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    GrabberInterpreter<StateGrabber,
                       MeasurementExpression<StateGrabber>,
                       MeasurementExpression<StateGrabber>>&
        stateGrabberInterpreter()
    {
        static GrabberInterpreter<StateGrabber,
                                  MeasurementExpression<StateGrabber>,
                                  MeasurementExpression<StateGrabber>>
            interpreter(
                [](std::string_view fld, CoreObject* obj) { return createStateGrabber(fld, obj); });
        return interpreter;
    }

    double secondaryRealPower(GridComponent* obj,
                              const StateData& stateDataValue,
                              const SolverMode& sMode)
    {
        return static_cast<GridSecondary*>(obj)->getRealPower(noInputs, stateDataValue, sMode);
    }

    double secondaryReactivePower(GridComponent* obj,
                                  const StateData& stateDataValue,
                                  const SolverMode& sMode)
    {
        return static_cast<GridSecondary*>(obj)->getReactivePower(noInputs, stateDataValue, sMode);
    }
}  // namespace

static constexpr char sepChars[] = ",;";

std::vector<std::unique_ptr<StateGrabber>> makeStateGrabbers(std::string_view command,
                                                             CoreObject* obj)
{
    std::vector<std::unique_ptr<StateGrabber>> grabbers;
    auto gsplit = splitGrabberList(command, sepChars);
    gmlc::utilities::stringOps::trim(gsplit);
    for (auto& cmd : gsplit) {
        if (const auto alias = cmd.find(" as "); alias != std::string::npos) {
            cmd.erase(alias);
        }
        auto sgb = stateGrabberInterpreter().interpretGrabberBlock(cmd, obj);
        if (sgb && sgb->loaded) {
            grabbers.push_back(std::move(sgb));
        }
    }
    return grabbers;
}

StateGrabber::StateGrabber(CoreObject* obj): cobj(dynamic_cast<GridComponent*>(obj)) {}
StateGrabber::StateGrabber(std::string_view fld, CoreObject* obj): StateGrabber(obj)
{
    StateGrabber::updateField(fld);
}

StateGrabber::StateGrabber(index_t noffset, CoreObject* obj):
    offset(noffset), cobj(dynamic_cast<GridComponent*>(obj))
{
    if (cobj != nullptr && noffset >= 0) {
        const auto names = cobj->localStateNames();
        if (static_cast<size_t>(noffset) < names.size()) {
            updateField(names[noffset]);
        }
    }
}

std::unique_ptr<StateGrabber> StateGrabber::clone() const
{
    auto stateGrabberClone = std::make_unique<StateGrabber>();
    cloneTo(stateGrabberClone.get());
    return stateGrabberClone;
}
void StateGrabber::cloneTo(StateGrabber* ggb) const
{
    ggb->field = field;
    ggb->fptr = fptr;
    ggb->jacIfptr = jacIfptr;
    ggb->gain = gain;
    ggb->bias = bias;
    ggb->inputUnits = inputUnits;
    ggb->outputUnits = outputUnits;
    ggb->loaded = loaded;
    ggb->vectorGrab = vectorGrab;
    ggb->cacheUpdateRequired = cacheUpdateRequired;
    ggb->offset = offset;
    ggb->jacMode = jacMode;
    ggb->prevIndex = prevIndex;
    ggb->cobj = cobj;
}

void StateGrabber::updateField(std::string_view fld)
{
    field = fld;
    vectorGrab = false;
    fptr = nullptr;
    jacIfptr = nullptr;
    jacMode = JacobianMode::NONE;
    cacheUpdateRequired = false;
    offset = kInvalidLocation;
    inputUnits = units::defunit;
    if (field == "constant") {
        fptr = [](GridComponent*, const StateData&, const SolverMode&) { return 0.0; };
        loaded = true;
        return;
    }
    if (cobj == nullptr) {
        loaded = false;
        return;
    }
    auto fieldDescription = gmlc::utilities::convertToLowerCase(std::string{fld});
    loaded = true;
    if (dynamic_cast<GridBus*>(cobj) != nullptr) {
        busLoadInfo(fieldDescription);
    } else if (dynamic_cast<Link*>(cobj) != nullptr) {
        linkLoadInfo(fieldDescription);
    } else if (dynamic_cast<GridSecondary*>(cobj) != nullptr) {
        secondaryLoadInfo(fieldDescription);
    } else if (dynamic_cast<Relay*>(cobj) != nullptr) {
        relayLoadInfo(fieldDescription);
    } else if (dynamic_cast<GridArea*>(cobj) != nullptr) {
        areaLoadInfo(fieldDescription);
    } else {
        objectLoadInfo(fieldDescription);
    }
    if (!loaded) {
        const auto stateIndex = cobj->findIndex(field, cLocalSolverMode);
        if (stateIndex != kInvalidLocation) {
            fptr = [stateField =
                        field](GridComponent* comp, const StateData& data, const SolverMode& mode) {
                const auto index = comp->findIndex(stateField, mode);
                if (index < 0) {
                    return kNullVal;
                }
                return isLocal(mode) || data.empty() ? comp->getState(index) : data.state[index];
            };
            jacIfptr = [stateField = field](GridComponent* comp,
                                            const StateData&,
                                            MatrixData<double>& matrix,
                                            const SolverMode& mode) {
                matrix.assignCheckCol(0, comp->findIndex(stateField, mode), 1.0);
            };
            jacMode = JacobianMode::DIRECT;
            loaded = true;
        }
    }
}

using units::defunit;
using units::puA;
using units::puHz;
using units::puMW;
using units::puOhm;
using units::puV;
using units::rad;

/** map of all the alternate strings that can be used*/
static const std::map<std::string, std::string> STRING_TRANSLATE{
    {"v", "voltage"},
    {"vol", "voltage"},
    {"link", "linkreal"},
    {"linkp", "linkreal"},
    {"loadq", "loadreactive"},
    {"loadreactivepower", "loadreactive"},
    {"load", "loadreal"},
    {"loadp", "loadreal"},
    {"reactivegen", "genreactive"},
    {"genq", "genreactive"},
    {"gen", "general"},
    {"generation", "general"},
    {"genp", "general"},
    {"realgen", "general"},
    {"f", "freq"},
    {"frequency", "freq"},
    {"omega", "freq"},
    {"a", "angle"},
    {"ang", "angle"},
    {"phase", "angle"},
    {"busgenerationreal", "busgenreal"},
    {"busp", "busgenreal"},
    {"busgen", "busgenreal"},
    {"busgenerationreactive", "busgenreactive"},
    {"busq", "busgenreactive"},
    {"linkrealpower", "linkreal"},
    {"linkp1", "linkreal"},
    {"linkq", "linkreactive"},
    {"linkreactivepower", "linkreactive"},
    {"linkrealpower1", "linkreal"},
    {"linkq1", "linkreactive"},
    {"linkreactivepower1", "linkreactive"},
    {"linkreal1", "linkreal"},
    {"linkreactive1", "linkreactive"},
    {"linkrealpower2", "linkreal2"},
    {"linkq2", "linkreactive2"},
    {"linkreactivepower2", "linkreactive2"},
    {"linkp2", "linkreal2"},
    {"p", "real"},
    {"q", "reactive"},
    {"impedance", "z"},
    {"admittance", "y"},
    {"impedance1", "z"},
    {"admittance1", "y"},
    {"z1", "z"},
    {"y1", "y"},
    {"impedance2", "z2"},
    {"admittance2", "y2"},
    {"status", "connected"},
    {"breaker", "switch"},
    {"breaker1", "switch"},
    {"switch1", "switch"},
    {"breaker2", "switch2"},
    {"i", "current"},
    {"i1", "current"},
    {"current1", "current"},
    {"i2", "current2"},
    {"imagcurrent1", "imagcurrent"},
    {"realcurrent1", "realcurrent"},
    {"lossreal", "loss"},
    {"angle1", "angle"},
    {"absangle1", "busangle"},
    {"voltage1", "voltage"},
    {"v1", "voltage"},
    {"v2", "voltage2"},
    {"output0", "output"},
    {"cv", "output"},
    {"o0", "output"},
    {"currentvalue", "output"},
    {"deriv0", "deriv"},
    {"dodt", "deriv"},
    {"dodt0", "deriv"},
    {"doutdt", "deriv"},
    {"doutdt0", "deriv"},
    {"busangle1", "busangle"},
    {"absangle", "busangle"},
    {"absangle1", "busangle2"},
};

#define FUNCTION_SIGNATURE                                                                         \
    [](GridComponent * obj, const StateData& stateDataValue, const SolverMode& sMode)
#define FUNCTION_SIGNATURE_OBJ_ONLY                                                                \
    [](GridComponent * obj, const StateData& /*sD*/, const SolverMode& /*sMode*/)

#define JAC_FUNCTION_SIGNATURE                                                                     \
    [](GridComponent * obj,                                                                        \
       const StateData& stateDataValue,                                                            \
       MatrixData<double>& matrixDataValue,                                                        \
       const SolverMode& sMode)
#define JAC_FUNCTION_SIGNATURE_NO_STATE                                                            \
    [](GridComponent * obj,                                                                        \
       const StateData& /*sD*/,                                                                    \
       MatrixData<double>& matrixDataValue,                                                        \
       const SolverMode& sMode)

// clang-format off
static const std::map<std::string, fstateobjectPair> OBJECT_FUNCTIONS{
  {"connected", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<double> (obj->isConnected ());}, defunit}},
  {"enabled", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<double> (obj->isEnabled ());}, defunit}},
{"armed", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<double> (obj->isArmed ());}, defunit}},
{"output", {FUNCTION_SIGNATURE{return obj->getOutput (noInputs, stateDataValue, sMode, 0);}, defunit}},
{"deriv",{FUNCTION_SIGNATURE { return obj->getDoutdt (noInputs, stateDataValue, sMode, 0); }, defunit}}
};

static const std::map<std::string, fstateobjectPair> BUS_FUNCTIONS{
  {"voltage", {FUNCTION_SIGNATURE{return static_cast<GridBus *> (obj)->getVoltage (stateDataValue, sMode);}, puV}},
{"angle", {FUNCTION_SIGNATURE{return static_cast<GridBus *> (obj)->getAngle (stateDataValue, sMode);}, rad}},
{"busangle",{ FUNCTION_SIGNATURE{ return static_cast<GridBus *> (obj)->getAngle(stateDataValue, sMode); }, rad } },
{"freq", {FUNCTION_SIGNATURE{return static_cast<GridBus *> (obj)->getFreq (stateDataValue, sMode);}, puHz}},
{ "busfreq",{ FUNCTION_SIGNATURE{ return static_cast<GridBus *> (obj)->getFreq(stateDataValue, sMode); }, puHz } },
{"general", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridBus *> (obj)->getGenerationReal ();}, puMW}},
{"genreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridBus *> (obj)->getGenerationReactive ();}, puMW}},
{"loadreal", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridBus *> (obj)->getLoadReal ();}, puMW}},
{"loadreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridBus *> (obj)->getLoadReactive ();}, puMW}},
{"linkreal", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridBus *> (obj)->getLinkReal ();}, puMW}},
{"linkreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridBus *> (obj)->getLinkReactive ();}, puMW}},
};

static const std::map<std::string, objJacFunction> BUS_JAC_FUNCTIONS{
  {"voltage", JAC_FUNCTION_SIGNATURE_NO_STATE{
          matrixDataValue.assignCheckCol (0, static_cast<GridBus *> (obj)->getOutputLoc (sMode, VOLTAGE_IN_LOCATION), 1.0);}},
  {"angle", JAC_FUNCTION_SIGNATURE_NO_STATE{
              matrixDataValue.assignCheckCol (0, static_cast<GridBus *> (obj)->getOutputLoc (sMode, ANGLE_IN_LOCATION), 1.0);}},
 { "busangle", JAC_FUNCTION_SIGNATURE_NO_STATE{
    matrixDataValue.assignCheckCol(0, static_cast<GridBus *> (obj)->getOutputLoc(sMode, ANGLE_IN_LOCATION), 1.0); } },
    { "freq", JAC_FUNCTION_SIGNATURE_NO_STATE{
    matrixDataValue.assignCheckCol(0, static_cast<GridBus *> (obj)->getOutputLoc(sMode, FREQUENCY_IN_LOCATION), 1.0); } },
    { "busfreq", JAC_FUNCTION_SIGNATURE_NO_STATE{
    matrixDataValue.assignCheckCol(0, static_cast<GridBus *> (obj)->getOutputLoc(sMode, FREQUENCY_IN_LOCATION), 1.0); } },
};

static const std::map<std::string, fstateobjectPair> AREA_FUNCTIONS{
  {"avgfreq", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getAvgFreq ();}, puHz}},
{"general", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getGenerationReal ();}, puMW}},
{"genreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getGenerationReactive ();}, puMW}},
{"loadreal", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getLoadReal ();}, puMW}},
{"loadreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getLoadReactive ();}, puMW}},
{"loss", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getLoss ();}, puMW}},
{"tieflow", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getTieFlowReal ();}, puMW}},
};

static const std::map<std::string, fstateobjectPair> SECONDARY_FUNCTIONS{ { "real",{ secondaryRealPower, puMW } },
{ "reactive",{ secondaryReactivePower, puMW } },
{ "busangle",{ FUNCTION_SIGNATURE{ return static_cast<GridSecondary*>(obj)->getBus()->getAngle(stateDataValue, sMode); }, rad } },
{ "busvoltage",{ FUNCTION_SIGNATURE{ return static_cast<GridSecondary*>(obj)->getBus()->getVoltage(stateDataValue, sMode); }, puV } },
{ "busfreq",{ FUNCTION_SIGNATURE{ return static_cast<GridSecondary*>(obj)->getBus()->getFreq(stateDataValue, sMode); }, puHz } },
};

static const std::map<std::string, fstateobjectPair> LOAD_FUNCTIONS{{"loadreal", {secondaryRealPower, puMW}},
                                                                   {"loadreactive", {secondaryReactivePower, puMW}}
};

static const std::map<std::string, fstateobjectPair> GEN_FUNCTIONS{
  {"general", {secondaryRealPower, puMW}},
  {"genreactive", {secondaryReactivePower, puMW}},
  {"pset", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Generator *> (obj)->getPset ();}, puMW}},
  {"adjup", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Generator *> (obj)->getAdjustableCapacityUp ();}, puMW}},
  {"adjdown", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Generator *> (obj)->getAdjustableCapacityDown ();}, puMW}},
  {"pmax", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Generator *> (obj)->getPmax ();}, puMW}},
  {"pmin", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Generator *> (obj)->getPmin ();}, puMW}},
  {"qmax", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Generator *> (obj)->getQmax ();}, puMW}},
  {"qmin", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Generator *> (obj)->getQmin ();}, puMW}},
  {"freq",{FUNCTION_SIGNATURE{return static_cast<Generator *> (obj)->getFreq (stateDataValue, sMode);}, puHz}},
  {"angle",{FUNCTION_SIGNATURE{return static_cast<Generator *> (obj)->getAngle (stateDataValue, sMode);}, rad}},
};

static const std::map<std::string, fstateobjectPair> LINK_FUNCTIONS{
  {"real", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getRealPower (1);}, puMW}},
{"reactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getReactivePower (1);}, puMW}},
{"linkreal", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getRealPower (1);}, puMW}},
{"linkreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getReactivePower (1);}, puMW}},
{"z", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getTotalImpedance (1);}, puOhm}},
{"y", {FUNCTION_SIGNATURE_OBJ_ONLY{return 1.0 / (static_cast<Link *> (obj)->getTotalImpedance (1));}, puOhm}},
{"r", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getRealImpedance (1);}, puOhm}},
{"x", {FUNCTION_SIGNATURE_OBJ_ONLY{return 1.0 / (static_cast<Link *> (obj)->getImagImpedance (1));}, puOhm}},
{"current", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getCurrent (1));}, puA}},
{"realcurrent", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getRealCurrent (1));}, puA}},
{"imagcurrent", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getImagCurrent (1));}, puA}},
{"voltage", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getVoltage (1));}, puV}},
{ "busvoltage",{ FUNCTION_SIGNATURE_OBJ_ONLY{ return (static_cast<Link *> (obj)->getVoltage(1)); }, puV } },
{"busangle", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getBusAngle (1));}, rad}},
  // functions for "to" side
  {"real2", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getRealPower (2);}, puMW}},
{"reactive2", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getReactivePower (2);}, puMW}},
{"linkreal2", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getRealPower (2);}, puMW}},
{"linkreactive2", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getReactivePower (2);}, puMW}},
{"z2", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getTotalImpedance (2);}, puOhm}},
{"y2", {FUNCTION_SIGNATURE_OBJ_ONLY{return 1.0 / (static_cast<Link *> (obj)->getTotalImpedance (2));}, puOhm}},
{"r2", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<Link *> (obj)->getRealImpedance (2);}, puOhm}},
{"x2", {FUNCTION_SIGNATURE_OBJ_ONLY{return 1.0 / (static_cast<Link *> (obj)->getImagImpedance (2));}, puOhm}},
{"current2", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getCurrent (2));}, puA}},
{"realcurrent2", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getRealCurrent (2));}, puA}},
{"imagcurrent2", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getImagCurrent (2));}, puA}},
{"voltage2", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getVoltage (2));}, puV}},
{"busangle2", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getBusAngle (2));}, rad}},

  // non numbered fields
  {"angle", {FUNCTION_SIGNATURE{return static_cast<Link *> (obj)->getAngle (stateDataValue.state, sMode);}, rad}},
{"loss", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getLoss ());}, puMW}},
{"lossreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return (static_cast<Link *> (obj)->getReactiveLoss ());}, puMW}},
  {"attached",
   {FUNCTION_SIGNATURE_OBJ_ONLY{
     return static_cast<double> (((!static_cast<Link *> (obj)->checkFlag (Link::SWITCH1_OPEN_FLAG)) ||
                                  (!static_cast<Link *> (obj)->checkFlag (Link::SWITCH2_OPEN_FLAG))) &&
                                 (static_cast<Link *> (obj)->isEnabled ()));}, defunit}},
};

// clang-format on

void StateGrabber::objectLoadInfo(std::string_view fld)
{
    auto funcfind = OBJECT_FUNCTIONS.find(std::string{fld});
    if (funcfind != OBJECT_FUNCTIONS.end()) {
        fptr = funcfind->second.first;
    } else {
        std::string fieldStr;
        const int num =
            gmlc::utilities::stringOps::trailingStringInt(std::string{fld}, fieldStr, 0);
        if ((fieldStr == "value") || (fieldStr == "output") || (fieldStr == "o")) {
            fptr = [num](GridComponent* comp,
                         const StateData& stateDataValue,
                         const SolverMode& sMode) {
                return comp->getOutput(noInputs, stateDataValue, sMode, static_cast<index_t>(num));
            };
        } else if ((fieldStr == "deriv") || (fieldStr == "doutdt") || (fieldStr == "derivative")) {
            fptr = [num](GridComponent* comp,
                         const StateData& stateDataValue,
                         const SolverMode& sMode) {
                return comp->getDoutdt(noInputs, stateDataValue, sMode, static_cast<index_t>(num));
            };
        } else {
            auto index = cobj->lookupOutputIndex(fieldStr);
            if (index != kNullLocation) {
                fptr = [index](GridComponent* comp,
                               const StateData& stateDataValue,
                               const SolverMode& sMode) {
                    return comp->getOutput(noInputs, stateDataValue, sMode, index);
                };
            } else {
                fptr = nullptr;
                loaded = false;
            }
        }
    }
}

void StateGrabber::busLoadInfo(std::string_view fld)
{
    auto fldString = std::string{fld};
    const std::string nfstr = mapFind(STRING_TRANSLATE, fldString, fldString);

    auto funcfind = BUS_FUNCTIONS.find(nfstr);
    if (funcfind != BUS_FUNCTIONS.end()) {
        fptr = funcfind->second.first;
        inputUnits = funcfind->second.second;
        loaded = true;
        auto jacfind = BUS_JAC_FUNCTIONS.find(nfstr);
        if (jacfind != BUS_JAC_FUNCTIONS.end()) {
            jacIfptr = jacfind->second;
            jacMode = JacobianMode::COMPUTED;
        }
    } else {
        objectLoadInfo(nfstr);
    }
}

void StateGrabber::linkLoadInfo(std::string_view fld)
{
    auto fldString = std::string{fld};
    const std::string nfstr = mapFind(STRING_TRANSLATE, fldString, fldString);

    auto funcfind = LINK_FUNCTIONS.find(nfstr);
    if (funcfind != LINK_FUNCTIONS.end()) {
        fptr = funcfind->second.first;
        inputUnits = funcfind->second.second;
        loaded = true;
        cacheUpdateRequired = true;
        /*auto jacfind = linkJacFunctions.find(nfstr);
        if (jacfind != BUS_JAC_FUNCTIONS.end())
        {
            jacIfptr = jacfind->second;
            jacMode = JacobianMode::COMPUTED;
        }
        */
    } else {
        objectLoadInfo(nfstr);
    }
}
void StateGrabber::relayLoadInfo(std::string_view fld)
{
    std::string fieldStr;
    const int num = gmlc::utilities::stringOps::trailingStringInt(std::string{fld}, fieldStr, 0);
    if ((fieldStr == "block") || (fieldStr == "b")) {
        if (dynamic_cast<Sensor*>(cobj) != nullptr) {
            fptr = [num](GridComponent* comp,
                         const StateData& stateDataValue,
                         const SolverMode& sMode) {
                return static_cast<Sensor*>(comp)->getBlockOutput(stateDataValue, sMode, num);
            };
        } else {
            loaded = false;
        }
    } else if ((fld == "blockderiv") || (fld == "dblockdt") || (fld == "dbdt")) {
        if (dynamic_cast<Sensor*>(cobj) != nullptr) {
            fptr = [num](GridComponent* comp,
                         const StateData& stateDataValue,
                         const SolverMode& sMode) {
                return static_cast<Sensor*>(comp)->getBlockDerivOutput(stateDataValue, sMode, num);
            };
        } else {
            loaded = false;
        }
    } else if ((fieldStr == "input") || (fieldStr == "i")) {
        if (dynamic_cast<Sensor*>(cobj) != nullptr) {
            fptr = [num](GridComponent* comp,
                         const StateData& stateDataValue,
                         const SolverMode& sMode) {
                return static_cast<Sensor*>(comp)->getInput(stateDataValue, sMode, num);
            };
        } else {
            loaded = false;
        }
    } else if ((fieldStr == "condition") || (fieldStr == "c")) {
        // dgptr = &Link::getAngle;
        fptr = [num](GridComponent* comp,
                     const StateData& stateDataValue,
                     const SolverMode& sMode) {
            return (static_cast<Relay*>(comp))->getCondition(num)->getVal(1, stateDataValue, sMode);
        };
    } else {
        objectLoadInfo(fld);
    }
}

void StateGrabber::secondaryLoadInfo(std::string_view fld)
{
    if ((fld == "realpower") || (fld == "power") || (fld == "p")) {
        cacheUpdateRequired = true;
        fptr = [](GridComponent* comp, const StateData& stateDataValue, const SolverMode& sMode) {
            return static_cast<GridSecondary*>(comp)->getRealPower(noInputs, stateDataValue, sMode);
        };
        jacMode = JacobianMode::COMPUTED;
        jacIfptr = [](GridComponent* comp,
                      const StateData& stateDataValue,
                      MatrixData<double>& matrixDataValue,
                      const SolverMode& sMode) {
            MatrixDataTranslate<1, double> translatedMatrix(matrixDataValue);
            translatedMatrix.setTranslation(POUT_LOCATION, 0);
            static_cast<GridSecondary*>(comp)->outputPartialDerivatives(noInputs,
                                                                        stateDataValue,
                                                                        translatedMatrix,
                                                                        sMode);
        };
    } else if ((fld == "reactivepower") || (fld == "reactive") || (fld == "q")) {
        cacheUpdateRequired = true;
        fptr = [](GridComponent* comp, const StateData& stateDataValue, const SolverMode& sMode) {
            return static_cast<GridSecondary*>(comp)->getReactivePower(noInputs,
                                                                       stateDataValue,
                                                                       sMode);
        };
        jacMode = JacobianMode::COMPUTED;
        jacIfptr = [](GridComponent* comp,
                      const StateData& stateDataValue,
                      MatrixData<double>& matrixDataValue,
                      const SolverMode& sMode) {
            MatrixDataTranslate<1, double> translatedMatrix(matrixDataValue);
            translatedMatrix.setTranslation(QOUT_LOCATION, 0);
            static_cast<GridSecondary*>(comp)->outputPartialDerivatives(noInputs,
                                                                        stateDataValue,
                                                                        translatedMatrix,
                                                                        sMode);
        };
    } else {
        loaded = false;  // resolve named states through the common field path
    }
}

void StateGrabber::areaLoadInfo(std::string_view fld)
{
    const auto name = mapFind(STRING_TRANSLATE, std::string{fld}, std::string{fld});
    const auto function = AREA_FUNCTIONS.find(name);
    if (function != AREA_FUNCTIONS.end()) {
        fptr = function->second.first;
        inputUnits = function->second.second;
    } else {
        objectLoadInfo(fld);
    }
}
double StateGrabber::grabData(const StateData& stateDataValue, const SolverMode& sMode)
{
    if (loaded && !vectorGrab && fptr) {
        if (cacheUpdateRequired) {
            cobj->updateLocalCache(noInputs, stateDataValue, sMode);
        }
        double val = fptr(cobj, stateDataValue, sMode);
        if (outputUnits != units::defunit) {
            val = GrabberUnitConversion(inputUnits, outputUnits, cobj).convert(val);
        }
        val = std::fma(val, gain, bias);
        return val;
    }
    return kNullVal;
}

void StateGrabber::grabVectorData(const StateData& /*data*/,
                                  const SolverMode& /*mode*/,
                                  std::vector<double>& values)
{
    values.clear();
}

void StateGrabber::outputVectorPartialDerivatives(const StateData& data,
                                                  MatrixData<double>& matrix,
                                                  const SolverMode& mode,
                                                  size_t element)
{
    if (!vectorGrab && element == 0) {
        outputPartialDerivatives(data, matrix, mode);
    }
}

void StateGrabber::getDesc(stringVec& descriptions) const
{
    descriptions.assign(1, cobj != nullptr ? cobj->getName() + ':' + field : field);
}

void StateGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (mode == ObjectUpdateMode::DIRECT) {
        cobj = dynamic_cast<GridComponent*>(obj);
    } else if (mode == ObjectUpdateMode::MATCH) {
        cobj = dynamic_cast<GridComponent*>(findMatchingObject(cobj, obj));
    }
    if (!field.empty()) {
        updateField(std::string{field});
    } else {
        loaded = cobj != nullptr && static_cast<bool>(fptr);
    }
}

CoreObject* StateGrabber::getObject() const
{
    return cobj;
}
void StateGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    objects.push_back(getObject());
}
void StateGrabber::outputPartialDerivatives(const StateData& stateDataValue,
                                            MatrixData<double>& matrixDataValue,
                                            const SolverMode& sMode)
{
    if (!loaded || jacMode == JacobianMode::NONE || !jacIfptr) {
        return;
    }
    double scale = gain;
    if (outputUnits != units::defunit) {
        const GrabberUnitConversion conversion(inputUnits, outputUnits, cobj);
        scale *= conversion.convert(1.0) - conversion.convert(0.0);
    }
    if (scale != 1.0) {
        MatrixDataScale<double> scaledMatrix(matrixDataValue, scale);
        jacIfptr(cobj, stateDataValue, scaledMatrix, sMode);
    } else {
        jacIfptr(cobj, stateDataValue, matrixDataValue, sMode);
    }
}

CustomStateGrabber::CustomStateGrabber(GridComponent* comp): StateGrabber(comp) {}
void CustomStateGrabber::setGrabberFunction(objStateGrabberFunction nfptr)
{
    fptr = std::move(nfptr);
    loaded = static_cast<bool>(fptr);
}

void CustomStateGrabber::setGrabberJacFunction(objJacFunction nJfptr)
{
    jacIfptr = std::move(nJfptr);
    jacMode = (jacIfptr) ? JacobianMode::COMPUTED : JacobianMode::NONE;
}

std::unique_ptr<StateGrabber> CustomStateGrabber::clone() const
{
    std::unique_ptr<StateGrabber> stateGrabberClone = std::make_unique<CustomStateGrabber>();
    cloneTo(stateGrabberClone.get());
    return stateGrabberClone;
}
void CustomStateGrabber::cloneTo(StateGrabber* ggb) const
{
    StateGrabber::cloneTo(ggb);
    auto* csg = dynamic_cast<CustomStateGrabber*>(ggb);
    if (csg == nullptr) {
        return;
    }
}

StateFunctionGrabber::StateFunctionGrabber(std::shared_ptr<StateGrabber> ggb, std::string func):
    function_name(std::move(func))
{
    if (ggb) {
        bgrabber = std::move(ggb);
    }
    updateField(std::string{function_name});
}

void StateFunctionGrabber::updateField(std::string_view fld)
{
    if (!fld.empty()) {
        function_name = fld;
        if (auto function = get1ArgFunction(fld)) {
            opptr = function;
            dopptr = getDerivative1ArgFunction(function_name);
        } else {
            opptr = nullptr;
            dopptr = nullptr;
            loaded = false;
        }
    }

    loaded = ((opptr != nullptr) && (bgrabber != nullptr) && bgrabber->loaded);
    jacMode = loaded && bgrabber->getJacobianMode() != JacobianMode::NONE ? JacobianMode::COMPUTED :
                                                                            JacobianMode::NONE;
}

std::unique_ptr<StateGrabber> StateFunctionGrabber::clone() const
{
    std::unique_ptr<StateGrabber> stateGrabberClone = std::make_unique<StateFunctionGrabber>();
    cloneTo(stateGrabberClone.get());
    return stateGrabberClone;
}

void StateFunctionGrabber::cloneTo(StateGrabber* ggb) const
{
    if (ggb == this) {
        return;
    }
    StateGrabber::cloneTo(ggb);
    auto* sfg = dynamic_cast<StateFunctionGrabber*>(ggb);
    if (sfg == nullptr) {
        return;
    }
    sfg->bgrabber = bgrabber ? bgrabber->clone() : nullptr;
    sfg->function_name = function_name;
    sfg->opptr = opptr;
    sfg->dopptr = dopptr;
}

double StateFunctionGrabber::grabData(const StateData& stateDataValue, const SolverMode& sMode)
{
    if (!loaded || opptr == nullptr || !bgrabber) {
        return kNullVal;
    }
    double val = opptr(bgrabber->grabData(stateDataValue, sMode));
    val = std::fma(val, gain, bias);
    return val;
}

void StateFunctionGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (bgrabber) {
        bgrabber->updateObject(obj, mode);
    }
    updateField(std::string{function_name});
}

CoreObject* StateFunctionGrabber::getObject() const
{
    return (bgrabber) ? bgrabber->getObject() : nullptr;
}

void StateFunctionGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    if (bgrabber) {
        bgrabber->getObjects(objects);
    }
}
void StateFunctionGrabber::outputPartialDerivatives(const StateData& stateDataValue,
                                                    MatrixData<double>& matrixDataValue,
                                                    const SolverMode& sMode)
{
    if (!loaded || opptr == nullptr || !bgrabber || jacMode == JacobianMode::NONE) {
        return;
    }

    const double temp = bgrabber->grabData(stateDataValue, sMode);
    const double derivativeOutput =
        (dopptr != nullptr) ? dopptr(temp) : (opptr(temp + 1e-7) - opptr(temp)) / 1e-7;

    MatrixDataScale<double> scaledMatrix(matrixDataValue, derivativeOutput * gain);
    bgrabber->outputPartialDerivatives(stateDataValue, scaledMatrix, sMode);
}

StateOpGrabber::StateOpGrabber(std::shared_ptr<StateGrabber> ggb1,
                               std::shared_ptr<StateGrabber> ggb2,
                               std::string operationName): op_name(std::move(operationName))
{
    if (ggb1) {
        bgrabber1 = std::move(ggb1);
    }
    if (ggb2) {
        bgrabber2 = std::move(ggb2);
    }
    updateField(std::string{op_name});
}

void StateOpGrabber::updateField(std::string_view opName)
{
    if (!opName.empty()) {
        op_name = opName;
        if (auto function = isPureFunction(opName) ? get2ArgFunction(opName) : nullptr) {
            opptr = function;
        } else {
            opptr = nullptr;
            loaded = false;
        }
    }

    loaded = ((opptr != nullptr) && (bgrabber1 != nullptr) && (bgrabber2 != nullptr) &&
              (bgrabber1->loaded) && (bgrabber2->loaded));
    jacMode = JacobianMode::NONE;
    if (loaded) {
        if (bgrabber1->field == "constant") {
            jacMode = bgrabber2->getJacobianMode();
        } else if (bgrabber2->field == "constant") {
            jacMode = bgrabber1->getJacobianMode();
        } else {
            jacMode = std::min(bgrabber1->getJacobianMode(), bgrabber2->getJacobianMode());
        }
        if (jacMode != JacobianMode::NONE) {
            jacMode = JacobianMode::COMPUTED;
        }
    }
}

std::unique_ptr<StateGrabber> StateOpGrabber::clone() const
{
    std::unique_ptr<StateGrabber> stateGrabberClone = std::make_unique<StateOpGrabber>();
    cloneTo(stateGrabberClone.get());
    return stateGrabberClone;
}

void StateOpGrabber::cloneTo(StateGrabber* ggb) const
{
    if (ggb == this) {
        return;
    }
    StateGrabber::cloneTo(ggb);
    auto* sog = dynamic_cast<StateOpGrabber*>(ggb);
    if (sog == nullptr) {
        return;
    }
    sog->bgrabber1 = bgrabber1 ? bgrabber1->clone() : nullptr;
    sog->bgrabber2 = bgrabber2 ? bgrabber2->clone() : nullptr;
    sog->op_name = op_name;
    sog->opptr = opptr;
}

double StateOpGrabber::grabData(const StateData& stateDataValue, const SolverMode& sMode)
{
    if (!loaded || opptr == nullptr) {
        return kNullVal;
    }
    const double grabber1Data = bgrabber1->grabData(stateDataValue, sMode);
    const double grabber2Data = bgrabber2->grabData(stateDataValue, sMode);
    double val = opptr(grabber1Data, grabber2Data);
    val = std::fma(val, gain, bias);
    return val;
}

void StateOpGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (bgrabber1) {
        bgrabber1->updateObject(obj, mode);
    }
    if (bgrabber2) {
        bgrabber2->updateObject(obj, mode);
    }
    updateField(std::string{op_name});
}

void StateOpGrabber::updateObject(CoreObject* obj, int num)
{
    if (num == 1) {
        if (bgrabber1) {
            bgrabber1->updateObject(obj);
        }
    } else if (num == 2) {
        if (bgrabber2) {
            bgrabber2->updateObject(obj);
        }
    }
    updateField(std::string{op_name});
}

CoreObject* StateOpGrabber::getObject() const
{
    if (bgrabber1) {
        return bgrabber1->getObject();
    }
    if (bgrabber2) {
        return bgrabber2->getObject();
    }
    return nullptr;
}

void StateOpGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    if (bgrabber1) {
        bgrabber1->getObjects(objects);
    }
    if (bgrabber2) {
        bgrabber2->getObjects(objects);
    }
}

void StateOpGrabber::outputPartialDerivatives(const StateData& stateDataValue,
                                              MatrixData<double>& matrixDataValue,
                                              const SolverMode& sMode)
{
    if (!loaded || opptr == nullptr || jacMode == JacobianMode::NONE) {
        return;
    }

    const double grabber1Data = bgrabber1->grabData(stateDataValue, sMode);
    const double grabber2Data = bgrabber2->grabData(stateDataValue, sMode);

    const double baseValue = opptr(grabber1Data, grabber2Data);
    const double perturbedFirstValue = opptr(grabber1Data + 1e-7, grabber2Data);

    const double firstDerivativeOutput = (perturbedFirstValue - baseValue) / 1e-7;

    MatrixDataScale<double> scaledMatrix(matrixDataValue, firstDerivativeOutput * gain);
    bgrabber1->outputPartialDerivatives(stateDataValue, scaledMatrix, sMode);

    const double perturbedSecondValue = opptr(grabber1Data, grabber2Data + 1e-7);
    const double secondDerivativeOutput = (perturbedSecondValue - baseValue) / 1e-7;
    scaledMatrix.setScale(secondDerivativeOutput * gain);
    bgrabber2->outputPartialDerivatives(stateDataValue, scaledMatrix, sMode);
}

}  // namespace griddyn
