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
#include "StateGrabber.h"
#include "gmlc/containers/mapOps.hpp"
#include "utilities/matrixDataScale.hpp"
#include "utilities/matrixDataTranslate.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    GrabberInterpreter<StateGrabber, StateOpGrabber, StateFunctionGrabber>&
        stateGrabberInterpreter()
    {
        static GrabberInterpreter<StateGrabber, StateOpGrabber, StateFunctionGrabber> interpreter(
            [](std::string_view fld, CoreObject* obj) {
                return std::make_unique<StateGrabber>(fld, obj);
            });
        return interpreter;
    }

    double secondaryRealPower(GridComponent* obj,
                              const stateData& stateDataValue,
                              const SolverMode& sMode)
    {
        return static_cast<gridSecondary*>(obj)->getRealPower(noInputs, stateDataValue, sMode);
    }

    double secondaryReactivePower(GridComponent* obj,
                                  const stateData& stateDataValue,
                                  const SolverMode& sMode)
    {
        return static_cast<gridSecondary*>(obj)->getReactivePower(noInputs, stateDataValue, sMode);
    }
}  // namespace

static const char specialChars[] = R"(:(+-/*\^?)";
static const char sepChars[] = ",;";

std::vector<std::unique_ptr<StateGrabber>> makeStateGrabbers(std::string_view command,
                                                             CoreObject* obj)
{
    std::vector<std::unique_ptr<StateGrabber>> grabbers;
    auto gsplit = gmlc::utilities::stringOps::splitlineBracket(std::string{command}, sepChars);
    gmlc::utilities::stringOps::trim(gsplit);
    for (auto& cmd : gsplit) {
        if (cmd.find_first_of(specialChars) != std::string::npos) {
            auto sgb = stateGrabberInterpreter().interpretGrabberBlock(cmd, obj);
            if (sgb && sgb->loaded) {
                grabbers.push_back(std::move(sgb));
            }
        } else {
            auto sgb = std::make_unique<StateGrabber>(cmd, dynamic_cast<GridComponent*>(obj));
            if (sgb && sgb->loaded) {
                grabbers.push_back(std::move(sgb));
            }
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
    ggb->cacheUpdateRequired = cacheUpdateRequired;
    ggb->offset = offset;
    ggb->jacMode = jacMode;
    ggb->prevIndex = prevIndex;
    ggb->cobj = cobj;
}

void StateGrabber::updateField(std::string_view fld)
{
    field = fld;
    auto fieldDescription = gmlc::utilities::convertToLowerCase(std::string{fld});
    loaded = true;
    if (dynamic_cast<GridBus*>(cobj) != nullptr) {
        busLoadInfo(fieldDescription);
    } else if (dynamic_cast<Link*>(cobj) != nullptr) {
        linkLoadInfo(fieldDescription);
    } else if (dynamic_cast<gridSecondary*>(cobj) != nullptr) {
        secondaryLoadInfo(fieldDescription);
    } else if (dynamic_cast<Relay*>(cobj) != nullptr) {
        relayLoadInfo(fieldDescription);
    } else {
        loaded = false;
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
static const std::map<std::string, std::string> stringTranslate{
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
    [](GridComponent * obj, const stateData& stateDataValue, const SolverMode& sMode)
#define FUNCTION_SIGNATURE_OBJ_ONLY                                                                \
    [](GridComponent * obj, const stateData& /*sD*/, const SolverMode& /*sMode*/)

#define JAC_FUNCTION_SIGNATURE                                                                     \
    [](GridComponent * obj,                                                                        \
       const stateData& stateDataValue,                                                            \
       matrixData<double>& matrixDataValue,                                                        \
       const SolverMode& sMode)
#define JAC_FUNCTION_SIGNATURE_NO_STATE                                                            \
    [](GridComponent * obj,                                                                        \
       const stateData& /*sD*/,                                                                    \
       matrixData<double>& matrixDataValue,                                                        \
       const SolverMode& sMode)

// clang-format off
static const std::map<std::string, fstateobjectPair> objectFunctions{
  {"connected", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<double> (obj->isConnected ());}, defunit}},
  {"enabled", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<double> (obj->isEnabled ());}, defunit}},
{"armed", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<double> (obj->isArmed ());}, defunit}},
{"output", {FUNCTION_SIGNATURE{return obj->getOutput (noInputs, stateDataValue, sMode, 0);}, defunit}},
{"deriv",{FUNCTION_SIGNATURE { return obj->getDoutdt (noInputs, stateDataValue, sMode, 0); }, defunit}}
};

static const std::map<std::string, fstateobjectPair> busFunctions{
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

static const std::map<std::string, objJacFunction> busJacFunctions{
  {"voltage", JAC_FUNCTION_SIGNATURE_NO_STATE{
          matrixDataValue.assignCheckCol (0, static_cast<GridBus *> (obj)->getOutputLoc (sMode, voltageInLocation), 1.0);}},
  {"angle", JAC_FUNCTION_SIGNATURE_NO_STATE{
              matrixDataValue.assignCheckCol (0, static_cast<GridBus *> (obj)->getOutputLoc (sMode, angleInLocation), 1.0);}},
 { "busangle", JAC_FUNCTION_SIGNATURE_NO_STATE{
    matrixDataValue.assignCheckCol(0, static_cast<GridBus *> (obj)->getOutputLoc(sMode, angleInLocation), 1.0); } },
    { "freq", JAC_FUNCTION_SIGNATURE_NO_STATE{
    matrixDataValue.assignCheckCol(0, static_cast<GridBus *> (obj)->getOutputLoc(sMode, frequencyInLocation), 1.0); } },
    { "busfreq", JAC_FUNCTION_SIGNATURE_NO_STATE{
    matrixDataValue.assignCheckCol(0, static_cast<GridBus *> (obj)->getOutputLoc(sMode, frequencyInLocation), 1.0); } },
};

static const std::map<std::string, fstateobjectPair> areaFunctions{
  {"avgfreq", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getAvgFreq ();}, puHz}},
{"general", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getGenerationReal ();}, puMW}},
{"genreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getGenerationReactive ();}, puMW}},
{"loadreal", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getLoadReal ();}, puMW}},
{"loadreactive", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getLoadReactive ();}, puMW}},
{"loss", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getLoss ();}, puMW}},
{"tieflow", {FUNCTION_SIGNATURE_OBJ_ONLY{return static_cast<GridArea *> (obj)->getTieFlowReal ();}, puMW}},
};

static const std::map<std::string, fstateobjectPair> secondaryFunctions{ { "real",{ secondaryRealPower, puMW } },
{ "reactive",{ secondaryReactivePower, puMW } },
{ "busangle",{ FUNCTION_SIGNATURE{ return static_cast<gridSecondary *>(obj)->getBus()->getAngle(stateDataValue, sMode); }, rad } },
{ "busvoltage",{ FUNCTION_SIGNATURE{ return static_cast<gridSecondary *>(obj)->getBus()->getVoltage(stateDataValue, sMode); }, puV } },
{ "busfreq",{ FUNCTION_SIGNATURE{ return static_cast<gridSecondary *>(obj)->getBus()->getFreq(stateDataValue, sMode); }, puV } },
};

static const std::map<std::string, fstateobjectPair> loadFunctions{{"loadreal", {secondaryRealPower, puMW}},
                                                                   {"loadreactive", {secondaryReactivePower, puMW}}
};

static const std::map<std::string, fstateobjectPair> genFunctions{
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

static const std::map<std::string, fstateobjectPair> linkFunctions{
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
     return static_cast<double> (((!static_cast<Link *> (obj)->checkFlag (Link::switch1_open_flag)) ||
                                  (!static_cast<Link *> (obj)->checkFlag (Link::switch2_open_flag))) &&
                                 (static_cast<Link *> (obj)->isEnabled ()));}, defunit}},
};

// clang-format on

void StateGrabber::objectLoadInfo(std::string_view fld)
{
    auto funcfind = objectFunctions.find(std::string{fld});
    if (funcfind != objectFunctions.end()) {
        fptr = funcfind->second.first;
    } else {
        std::string fieldStr;
        const int num =
            gmlc::utilities::stringOps::trailingStringInt(std::string{fld}, fieldStr, 0);
        if ((fieldStr == "value") || (fieldStr == "output") || (fieldStr == "o")) {
            fptr = [num](GridComponent* comp,
                         const stateData& stateDataValue,
                         const SolverMode& sMode) {
                return comp->getOutput(noInputs, stateDataValue, sMode, static_cast<index_t>(num));
            };
        }
        if ((fieldStr == "deriv") || (fieldStr == "doutdt") || (fieldStr == "derivative")) {
            fptr = [num](GridComponent* comp,
                         const stateData& stateDataValue,
                         const SolverMode& sMode) {
                return comp->getDoutdt(noInputs, stateDataValue, sMode, static_cast<index_t>(num));
            };
        } else {
            auto index = cobj->lookupOutputIndex(fieldStr);
            if (index != kNullLocation) {
                fptr = [index](GridComponent* comp,
                               const stateData& stateDataValue,
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
    const std::string nfstr = mapFind(stringTranslate, fldString, fldString);

    auto funcfind = busFunctions.find(nfstr);
    if (funcfind != busFunctions.end()) {
        fptr = funcfind->second.first;
        inputUnits = funcfind->second.second;
        loaded = true;
        auto jacfind = busJacFunctions.find(nfstr);
        if (jacfind != busJacFunctions.end()) {
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
    const std::string nfstr = mapFind(stringTranslate, fldString, fldString);

    auto funcfind = linkFunctions.find(nfstr);
    if (funcfind != linkFunctions.end()) {
        fptr = funcfind->second.first;
        inputUnits = funcfind->second.second;
        loaded = true;
        cacheUpdateRequired = true;
        /*auto jacfind = linkJacFunctions.find(nfstr);
        if (jacfind != busJacFunctions.end())
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
        if (dynamic_cast<sensor*>(cobj) != nullptr) {
            fptr = [num](GridComponent* comp,
                         const stateData& stateDataValue,
                         const SolverMode& sMode) {
                return static_cast<sensor*>(comp)->getBlockOutput(stateDataValue, sMode, num);
            };
        } else {
            loaded = false;
        }
    } else if ((fld == "blockderiv") || (fld == "dblockdt") || (fld == "dbdt")) {
        if (dynamic_cast<sensor*>(cobj) != nullptr) {
            fptr = [num](GridComponent* comp,
                         const stateData& stateDataValue,
                         const SolverMode& sMode) {
                return static_cast<sensor*>(comp)->getBlockDerivOutput(stateDataValue, sMode, num);
            };
        } else {
            loaded = false;
        }
    } else if ((fieldStr == "input") || (fieldStr == "i")) {
        if (dynamic_cast<sensor*>(cobj) != nullptr) {
            fptr = [num](GridComponent* comp,
                         const stateData& stateDataValue,
                         const SolverMode& sMode) {
                return static_cast<sensor*>(comp)->getInput(stateDataValue, sMode, num);
            };
        } else {
            loaded = false;
        }
    } else if ((fieldStr == "condition") || (fieldStr == "c")) {
        // dgptr = &Link::getAngle;
        fptr = [num](GridComponent* comp,
                     const stateData& stateDataValue,
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
        fptr = [](GridComponent* comp, const stateData& stateDataValue, const SolverMode& sMode) {
            return static_cast<gridSecondary*>(comp)->getRealPower(noInputs, stateDataValue, sMode);
        };
        jacMode = JacobianMode::COMPUTED;
        jacIfptr = [](GridComponent* comp,
                      const stateData& stateDataValue,
                      matrixData<double>& matrixDataValue,
                      const SolverMode& sMode) {
            matrixDataTranslate<1, double> translatedMatrix(matrixDataValue);
            translatedMatrix.setTranslation(PoutLocation, 0);
            static_cast<gridSecondary*>(comp)->outputPartialDerivatives(noInputs,
                                                                        stateDataValue,
                                                                        translatedMatrix,
                                                                        sMode);
        };
    } else if ((fld == "reactivepower") || (fld == "reactive") || (fld == "q")) {
        cacheUpdateRequired = true;
        fptr = [](GridComponent* comp, const stateData& stateDataValue, const SolverMode& sMode) {
            return static_cast<gridSecondary*>(comp)->getReactivePower(noInputs,
                                                                       stateDataValue,
                                                                       sMode);
        };
        jacMode = JacobianMode::COMPUTED;
        jacIfptr = [](GridComponent* comp,
                      const stateData& stateDataValue,
                      matrixData<double>& matrixDataValue,
                      const SolverMode& sMode) {
            matrixDataTranslate<1, double> translatedMatrix(matrixDataValue);
            translatedMatrix.setTranslation(QoutLocation, 0);
            static_cast<gridSecondary*>(comp)->outputPartialDerivatives(noInputs,
                                                                        stateDataValue,
                                                                        translatedMatrix,
                                                                        sMode);
        };
    } else {
        offset = static_cast<gridSecondary*>(cobj)->findIndex(fld, cLocalSolverMode);
        if (offset != kInvalidLocation) {
            prevIndex = 1;
            fptr = [this](GridComponent* comp,
                          const stateData& stateDataValue,
                          const SolverMode& sMode) {
                if (sMode.offsetIndex != prevIndex) {
                    offset = static_cast<gridSecondary*>(comp)->findIndex(field, sMode);
                    prevIndex = sMode.offsetIndex;
                }
                return (offset != kNullLocation) ? stateDataValue.state[offset] : kNullVal;
            };
            jacMode = JacobianMode::COMPUTED;
            jacIfptr = [this](GridComponent* /*comp*/,
                              const stateData& /*sD*/,
                              matrixData<double>& matrixDataValue,
                              const SolverMode& /*sMode*/) {
                matrixDataValue.assignCheckCol(0, offset, 1.0);
            };
        } else {
            loaded = false;
        }
    }
}

void StateGrabber::areaLoadInfo(std::string_view /*fld*/) {}
double StateGrabber::grabData(const StateData& stateDataValue, const SolverMode& sMode)
{
    if (loaded) {
        if (cacheUpdateRequired) {
            cobj->updateLocalCache(noInputs, stateDataValue, sMode);
        }
        double val = fptr(cobj, stateDataValue, sMode);
        val = std::fma(val, gain, bias);
        return val;
    }
    return kNullVal;
}

void StateGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (mode == ObjectUpdateMode::DIRECT) {
        cobj = dynamic_cast<GridComponent*>(obj);
    } else if (mode == ObjectUpdateMode::MATCH) {
        cobj = dynamic_cast<GridComponent*>(findMatchingObject(cobj, obj));
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
                                            matrixData<double>& matrixDataValue,
                                            const SolverMode& sMode)
{
    if (jacMode == JacobianMode::NONE) {
        return;
    }
    if (gain != 1.0) {
        matrixDataScale<double> scaledMatrix(matrixDataValue, gain);
        jacIfptr(cobj, stateDataValue, scaledMatrix, sMode);
    } else {
        jacIfptr(cobj, stateDataValue, matrixDataValue, sMode);
    }
}

CustomStateGrabber::CustomStateGrabber(GridComponent* comp): StateGrabber(comp) {}
void CustomStateGrabber::setGrabberFunction(objStateGrabberFunction nfptr)
{
    fptr = std::move(nfptr);
    loaded = true;
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
    opptr = get1ArgFunction(function_name);
    dopptr = getDerivative1ArgFunction(function_name);
    if (bgrabber) {
        jacMode = bgrabber->getJacobianMode();
        loaded = ((opptr != nullptr) && bgrabber->loaded);
    } else {
        loaded = false;
    }
}

void StateFunctionGrabber::updateField(std::string_view fld)
{
    if (!fld.empty()) {
        if (auto function = get1ArgFunction(fld)) {
            function_name = fld;
            opptr = function;
            dopptr = getDerivative1ArgFunction(function_name);
        } else {
            opptr = nullptr;
            dopptr = nullptr;
            loaded = false;
        }
    }

    loaded = ((opptr != nullptr) && (bgrabber != nullptr) && bgrabber->loaded);
}

std::unique_ptr<StateGrabber> StateFunctionGrabber::clone() const
{
    std::unique_ptr<StateGrabber> stateGrabberClone = std::make_unique<StateFunctionGrabber>();
    cloneTo(stateGrabberClone.get());
    return stateGrabberClone;
}

void StateFunctionGrabber::cloneTo(StateGrabber* ggb) const
{
    StateGrabber::cloneTo(ggb);
    auto* sfg = dynamic_cast<StateFunctionGrabber*>(ggb);
    if (sfg == nullptr) {
        return;
    }
    if (bgrabber) {
        if (sfg->bgrabber) {
            bgrabber->cloneTo(sfg->bgrabber.get());
        } else {
            sfg->bgrabber = bgrabber->clone();
        }
    }
    sfg->function_name = function_name;
    sfg->opptr = opptr;
    sfg->dopptr = dopptr;
}

double StateFunctionGrabber::grabData(const StateData& stateDataValue, const SolverMode& sMode)
{
    double val = opptr(bgrabber->grabData(stateDataValue, sMode));
    val = std::fma(val, gain, bias);
    return val;
}

void StateFunctionGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (bgrabber) {
        bgrabber->updateObject(obj, mode);
    }
}

CoreObject* StateFunctionGrabber::getObject() const
{
    return (bgrabber) ? bgrabber->getObject() : nullptr;
}
void StateFunctionGrabber::outputPartialDerivatives(const StateData& stateDataValue,
                                                    matrixData<double>& matrixDataValue,
                                                    const SolverMode& sMode)
{
    if (jacMode == JacobianMode::NONE) {
        return;
    }

    const double temp = bgrabber->grabData(stateDataValue, sMode);
    const double derivativeOutput =
        (dopptr != nullptr) ? dopptr(temp) : (opptr(temp + 1e-7) - opptr(temp)) / 1e-7;

    matrixDataScale<double> scaledMatrix(matrixDataValue, derivativeOutput * gain);
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
    opptr = get2ArgFunction(op_name);
    if ((bgrabber1 != nullptr) && (bgrabber2 != nullptr)) {
        jacMode = std::min(bgrabber1->getJacobianMode(), bgrabber2->getJacobianMode());
        loaded = ((opptr != nullptr) && (bgrabber1->loaded) && (bgrabber2->loaded));
    } else {
        loaded = false;
    }
}

void StateOpGrabber::updateField(std::string_view opName)
{
    if (!opName.empty()) {
        if (auto function = get2ArgFunction(opName)) {
            op_name = opName;
            opptr = function;
        } else {
            opptr = nullptr;
            loaded = false;
        }
    }

    loaded = ((opptr != nullptr) && (bgrabber1 != nullptr) && (bgrabber2 != nullptr) &&
              (bgrabber1->loaded) && (bgrabber2->loaded));
}

std::unique_ptr<StateGrabber> StateOpGrabber::clone() const
{
    std::unique_ptr<StateGrabber> stateGrabberClone = std::make_unique<StateOpGrabber>();
    cloneTo(stateGrabberClone.get());
    return stateGrabberClone;
}

void StateOpGrabber::cloneTo(StateGrabber* ggb) const
{
    StateGrabber::cloneTo(ggb);
    auto* sog = dynamic_cast<StateOpGrabber*>(ggb);
    if (sog == nullptr) {
        return;
    }
    if (bgrabber1) {
        if (sog->bgrabber1) {
            bgrabber1->cloneTo(sog->bgrabber1.get());
        } else {
            sog->bgrabber1 = bgrabber1->clone();
        }
    }
    if (bgrabber2) {
        if (sog->bgrabber2) {
            bgrabber2->cloneTo(sog->bgrabber2.get());
        } else {
            sog->bgrabber2 = bgrabber2->clone();
        }
    }
    sog->op_name = op_name;
    sog->opptr = opptr;
}

double StateOpGrabber::grabData(const StateData& stateDataValue, const SolverMode& sMode)
{
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

void StateOpGrabber::outputPartialDerivatives(const StateData& stateDataValue,
                                              matrixData<double>& matrixDataValue,
                                              const SolverMode& sMode)
{
    if (jacMode == JacobianMode::NONE) {
        return;
    }

    const double grabber1Data = bgrabber1->grabData(stateDataValue, sMode);
    const double grabber2Data = bgrabber2->grabData(stateDataValue, sMode);

    const double baseValue = opptr(grabber1Data, grabber2Data);
    const double perturbedFirstValue = opptr(grabber1Data + 1e-7, grabber2Data);

    const double firstDerivativeOutput = (perturbedFirstValue - baseValue) / 1e-7;

    matrixDataScale<double> scaledMatrix(matrixDataValue, firstDerivativeOutput * gain);
    bgrabber1->outputPartialDerivatives(stateDataValue, scaledMatrix, sMode);

    const double perturbedSecondValue = opptr(grabber1Data, grabber2Data + 1e-7);
    const double secondDerivativeOutput = (perturbedSecondValue - baseValue) / 1e-7;
    scaledMatrix.setScale(secondDerivativeOutput * gain);
    bgrabber2->outputPartialDerivatives(stateDataValue, scaledMatrix, sMode);
}

}  // namespace griddyn
