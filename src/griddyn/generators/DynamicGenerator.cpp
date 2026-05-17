/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DynamicGenerator.h"

#include "../Source.h"
#include "../Stabilizer.h"
#include "../controllers/scheduler.h"
#include "../exciters/ExciterDC2A.h"
#include "../genmodels/otherGenModels.h"
#include "../governors/GovernorTypes.h"
#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "isocController.h"
#include "utilities/matrixDataScale.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// #include <set>
/*
For the dynamics states order matters for entries used across
multiple components and other parts of the program.

genModel
[theta, V, Id, Iq, delta, w]

exciter
[Ef]

governor --- Pm(t0) = Pset is stored externally as well
[Pm]
*/

namespace griddyn {
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static typeFactory<DynamicGenerator>
    generatorFactory("generator", std::to_array<std::string_view>({"local_dynamic"}));

using units::convert;
using units::MVAR;
using units::MW;
using units::puMW;
using units::puV;
using units::rad;
using units::s;
using units::unit;

// default bus object

DynamicGenerator::DynamicGenerator(const std::string& objName): Generator(objName) {}

DynamicGenerator::DynamicGenerator(dynModel_t dynModel, const std::string& objName):
    DynamicGenerator(objName)
{
    buildDynModel(dynModel);
}
CoreObject* DynamicGenerator::clone(CoreObject* obj) const
{
    auto* gen = cloneBaseFactory<DynamicGenerator, Generator>(this, obj, &generatorFactory);
    if (gen == nullptr) {
        return obj;
    }
    return gen;
}
namespace {
    const auto& getDynModelFromStringMap()
    {
        static const std::
            map<std::string_view, DynamicGenerator::dynModel_t, std::less<std::string_view>>
                dynModelFromStringMap{
                    {"typical", DynamicGenerator::dynModel_t::typical},
                    {"simple", DynamicGenerator::dynModel_t::simple},
                    {"model_only", DynamicGenerator::dynModel_t::model_only},
                    {"modelonly", DynamicGenerator::dynModel_t::model_only},
                    {"transient", DynamicGenerator::dynModel_t::transient},
                    {"subtransient", DynamicGenerator::dynModel_t::detailed},
                    {"detailed", DynamicGenerator::dynModel_t::detailed},
                    {"none", DynamicGenerator::dynModel_t::none},
                    {"dc", DynamicGenerator::dynModel_t::dc},
                    {"renewable", DynamicGenerator::dynModel_t::renewable},
                    {"variable", DynamicGenerator::dynModel_t::renewable},
                };
        return dynModelFromStringMap;
    }
}  // namespace

DynamicGenerator::dynModel_t DynamicGenerator::dynModelFromString(const std::string& dynModelType)
{
    const auto str = gmlc::utilities::convertToLowerCase(dynModelType);
    const auto& dynModelFromStringMap = getDynModelFromStringMap();
    const auto foundModel = dynModelFromStringMap.find(str);
    return (foundModel != dynModelFromStringMap.end()) ? foundModel->second : dynModel_t::invalid;
}

void DynamicGenerator::buildDynModel(dynModel_t dynModel)
{
    switch (dynModel) {
        case dynModel_t::simple:
            if (gov == nullptr) {
                add(new Governor());
            }
            if (ext == nullptr) {
                add(new Exciter());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModelClassical());
            }

            break;
        case dynModel_t::dc:
            if (gov == nullptr) {
                add(new governors::GovernorIeeeSimple());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModelClassical());
            }

            break;
        case dynModel_t::typical:
            if (gov == nullptr) {
                add(new governors::GovernorIeeeSimple());
            }
            if (ext == nullptr) {
                add(new exciters::ExciterIEEEtype1());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModel4());
            }
            break;
        case dynModel_t::renewable:
            if (gov == nullptr) {
                add(new Governor());
            }
            if (ext == nullptr) {
                add(new Exciter());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModelInverter());
            }
            break;
        case dynModel_t::transient:
            if (gov == nullptr) {
                add(new governors::GovernorTgov1());
            }
            if (ext == nullptr) {
                add(new exciters::ExciterIEEEtype1());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModel5());
            }
            break;
        case dynModel_t::subtransient:
            if (gov == nullptr) {
                add(new governors::GovernorTgov1());
            }
            if (ext == nullptr) {
                add(new exciters::ExciterIEEEtype1());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModel6());
            }
            break;
        case dynModel_t::detailed:
            if (gov == nullptr) {
                add(new governors::GovernorTgov1());
            }
            if (ext == nullptr) {
                add(new exciters::ExciterIEEEtype1());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModel8());
            }
            break;
        case dynModel_t::model_only:
            if (genModel == nullptr) {
                add(new genmodels::GenModel4());
            }
            break;
        case dynModel_t::none:
            if (genModel == nullptr) {
                add(new GenModel());
            }
            break;
        case dynModel_t::invalid:
        default:
            break;
    }
}

void DynamicGenerator::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (machineBasePower < 0) {
        machineBasePower = systemBasePower;
    }
    // automatically define a trivial generator model if none has been specified
    if (genModel == nullptr) {
        add(new GenModel());
    }
    if (gov != nullptr) {
        if (!genModel->checkFlag(GenModel::genModel_flags::internal_frequency_calculation)) {
            opFlags.set(uses_bus_frequency);
        }
    }
    if (opFlags[isochronous_operation]) {
        bus->setFlag("compute_frequency", true);
        // opFlags.set(uses_bus_frequency);
    }
    gridSecondary::dynObjectInitializeA(time0, flags);  // NOLINT
}

// initial conditions of dynamic states
void DynamicGenerator::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& desiredOutput,
                                            IOdata& fieldSet)
{
    Generator::dynObjectInitializeB(inputs, desiredOutput, fieldSet);

    // load the power set point
    if (opFlags[isochronous_operation]) {
        if (Pset > -kHalfBigNum) {
            isoc->setLevel(P - Pset);
            isoc->setFreq(0.0);
        } else {
            isoc->setLevel(0.0);
            isoc->setFreq(0.0);
            Pset = P;
        }
    }

    const double scale = systemBasePower / machineBasePower;
    IOdata modelInputs(4);
    IOdata localDesiredOutput(4);

    const double voltage = inputs[voltageInLocation];
    const double theta = inputs[angleInLocation];

    modelInputs[voltageInLocation] = voltage;
    modelInputs[angleInLocation] = theta;
    modelInputs[genModelPmechInLocation] = kNullVal;
    modelInputs[genModelEftInLocation] = kNullVal;

    localDesiredOutput[PoutLocation] = P * scale;
    localDesiredOutput[QoutLocation] = Q * scale;

    IOdata computedFieldSet(4);
    genModel->dynInitializeB(modelInputs, localDesiredOutput, computedFieldSet);
    m_Pmech = computedFieldSet[genModelPmechInLocation];

    m_Eft = computedFieldSet[genModelEftInLocation];
    //  genModel->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);

    Pset = m_Pmech / scale;
    if (isoc != nullptr) {
        Pset -= isoc->getOutput();
    }

    if ((ext != nullptr) && (ext->isEnabled())) {
        modelInputs[voltageInLocation] = voltage;
        modelInputs[angleInLocation] = theta;
        modelInputs[exciterPmechInLocation] = m_Pmech;

        localDesiredOutput[0] = m_Eft;
        ext->dynInitializeB(modelInputs, localDesiredOutput, computedFieldSet);

        //    ext->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
        // Vset=inputSetup[1];
    }
    if ((gov != nullptr) && (gov->isEnabled())) {
        modelInputs[govOmegaInLocation] = systemBaseFrequency;
        modelInputs[govpSetInLocation] = kNullVal;

        localDesiredOutput[0] = Pset * scale;
        if (isoc != nullptr) {
            localDesiredOutput[0] += isoc->getOutput() * scale;
        }
        gov->dynInitializeB(modelInputs, localDesiredOutput, computedFieldSet);

        //     gov->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
    }

    if ((pss != nullptr) && (pss->isEnabled())) {
        modelInputs[0] = systemBaseFrequency;
        modelInputs[1] = kNullVal;
        localDesiredOutput[0] = 0;
        pss->dynInitializeB(modelInputs, desiredOutput, computedFieldSet);
        //    pss->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
    }

    modelInputs.resize(0);
    localDesiredOutput.resize(0);
    for (auto* sub : getSubObjects()) {
        if (sub->locIndex < 4) {
            continue;
        }
        if (sub->isEnabled()) {
            sub->dynInitializeB(modelInputs, localDesiredOutput, computedFieldSet);
            //    sub->guessState (prevTime, m_state.data (), m_dstate_dt.data (),
            //    cLocalbSolverMode);
        }
    }

    //  m_stateTemp = m_state.data ();
    // m_dstate_dt_Temp = m_dstate_dt.data ();
}

// save an external state to the internal one
void DynamicGenerator::setState(coreTime time,
                                const double state[],
                                const double dstate_dt[],
                                const solverMode& sMode)
{
    if (isDynamic(sMode)) {
        for (auto* subobj : getSubObjects()) {
            if (subobj->isEnabled()) {
                subobj->setState(time, state, dstate_dt, sMode);
                // subobj->guessState (time, m_state.data (), m_dstate_dt.data (),
                // cLocalbSolverMode);
            }
        }
        Pset += dPdt * (time - prevTime);
        Pset = gmlc::utilities::valLimit(Pset, Pmin, Pmax);
        updateLocalCache(noInputs, emptyStateData, cLocalSolverMode);
    } else if (stateSize(sMode) > 0) {
        Generator::setState(time, state, dstate_dt, sMode);
    }
    prevTime = time;
}

void DynamicGenerator::updateLocalCache(const IOdata& inputs,
                                        const stateData& stateDataValue,
                                        const solverMode& sMode)
{
    if ((isDynamic(sMode)) && (stateDataValue.updateRequired(subInputs.seqID))) {
        generateSubModelInputs(inputs, stateDataValue, sMode);  // generate current input values
        for (auto* subobj : getSubObjects()) {
            if (subobj->isEnabled()) {
                subobj->updateLocalCache(subInputs.inputs[subobj->locIndex], stateDataValue, sMode);
            }
        }
        // generate updated input values which in many cases will be the same as before
        generateSubModelInputs(inputs, stateDataValue, sMode);
        const double scale = machineBasePower / systemBasePower;
        P = -genModel->getOutput(subInputs.inputs[genmodel_loc],
                                 stateDataValue,
                                 sMode,
                                 PoutLocation) *
            scale;
        Q = -genModel->getOutput(subInputs.inputs[genmodel_loc],
                                 stateDataValue,
                                 sMode,
                                 QoutLocation) *
            scale;
    }
}

// copy the current state to a vector
void DynamicGenerator::guessState(coreTime time,
                                  double state[],
                                  double dstate_dt[],
                                  const solverMode& sMode)
{
    if (isDynamic(sMode)) {
        for (auto* subobj : getSubObjects()) {
            if (subobj->isEnabled()) {
                subobj->guessState(time, state, dstate_dt, sMode);
                // subobj->guessState (time, m_state.data (), m_dstate_dt.data (),
                // cLocalbSolverMode);
            }
        }
    } else if (stateSize(sMode) > 0) {
        Generator::guessState(time, state, dstate_dt, sMode);
    }
}

void DynamicGenerator::add(CoreObject* obj)
{
    Generator::add(obj);
}

void DynamicGenerator::add(gridSubModel* obj)
{
    if (dynamic_cast<Exciter*>(obj) != nullptr) {
        ext = static_cast<Exciter*>(replaceModel(obj, ext, exciter_loc));
    } else if (dynamic_cast<GenModel*>(obj) != nullptr) {
        genModel = static_cast<GenModel*>(replaceModel(obj, genModel, genmodel_loc));
        if (m_Rs != 0.0) {
            obj->set("rs", m_Rs);
        }
        if (m_Xs != 1.0) {
            obj->set("xs", m_Xs);
        }
    } else if (dynamic_cast<Governor*>(obj) != nullptr) {
        gov = static_cast<Governor*>(replaceModel(obj, gov, governor_loc));
        // mesh up the Pmax and Pmin giving priority to the new gov
        const double govpmax = gov->get("pmax");
        const double govpmin = gov->get("pmin");
        if (govpmax < kHalfBigNum) {
            Pmax = govpmax * machineBasePower / systemBasePower;
            Pmin = govpmin * machineBasePower / systemBasePower;
        } else {
            gov->set("pmax", Pmax * systemBasePower / machineBasePower);
            gov->set("pmin", Pmin * systemBasePower / machineBasePower);
        }
    } else if (dynamic_cast<Stabilizer*>(obj) != nullptr) {
        pss = static_cast<Stabilizer*>(replaceModel(obj, pss, pss_loc));
    } else if (dynamic_cast<Source*>(obj) != nullptr) {
        auto* src = static_cast<Source*>(obj);
        if ((src->purpose_ == "power") || (src->purpose_ == "pset")) {
            pSetControl = static_cast<Source*>(replaceModel(obj, pSetControl, pset_loc));
            if (dynamic_cast<scheduler*>(pSetControl) != nullptr) {
                sched = static_cast<scheduler*>(pSetControl);
            }
        } else if ((src->purpose_ == "voltage") || (src->purpose_ == "vset")) {
            vSetControl = static_cast<Source*>(replaceModel(obj, vSetControl, vset_loc));
        } else if ((pSetControl == nullptr) && (src->purpose_.empty())) {
            pSetControl = static_cast<Source*>(replaceModel(obj, pSetControl, pset_loc));
        } else {
            throw(objectAddFailure(this));
        }
    } else if (dynamic_cast<isocController*>(obj) != nullptr) {
        isoc = static_cast<isocController*>(replaceModel(obj, isoc, isoc_control));
        subInputLocs.inputLocs[isoc_control].resize(1);
        subInputs.inputs[isoc_control].resize(1);
    } else {
        throw(unrecognizedObjectException(this));
    }
}

gridSubModel* DynamicGenerator::replaceModel(gridSubModel* newObject,
                                             gridSubModel* oldObject,
                                             index_t newIndex)
{
    replaceSubObject(newObject, oldObject);
    newObject->locIndex = newIndex;

    if (std::cmp_greater_equal(newIndex, subInputs.inputs.size())) {
        subInputs.inputs.resize(newIndex + 1);
        subInputLocs.inputLocs.resize(newIndex + 1);
    }
    return newObject;
}

// set properties
void DynamicGenerator::set(std::string_view param, std::string_view val)
{
    if (param == "dynmodel") {
        auto dmodel = dynModelFromString(std::string{val});
        if (dmodel == dynModel_t::invalid) {
            throw(invalidParameterValue(val));
        }
        buildDynModel(dmodel);
    } else {
        try {
            Generator::set(param, val);
        }
        catch (const std::invalid_argument& ia) {
            bool setSuccess = false;
            for (auto* subobj : getSubObjects()) {
                subobj->setFlag("no_gridcomponent_set");
                try {
                    subobj->set(param, val);
                    subobj->setFlag("no_gridcomponent_set", false);
                    setSuccess = true;
                    break;
                }
                catch (const std::invalid_argument&) {
                    subobj->setFlag("no_gridcomponent_set", false);
                }
            }
            if (!setSuccess) {
                throw ia;
            }
        }
    }
}

void DynamicGenerator::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    Generator::timestep(time, inputs, sMode);
    if (isDynamic(sMode)) {
        const double scale = machineBasePower / systemBasePower;
        const double omega = genModel->getFreq(emptyStateData, cLocalSolverMode);

        if ((gov != nullptr) && (gov->isEnabled())) {
            gov->timestep(time, {omega, Pset / scale}, sMode);
            m_Pmech = gov->getOutput();
        }

        if ((ext != nullptr) && (ext->isEnabled())) {
            ext->timestep(time,
                          {inputs[voltageInLocation], inputs[angleInLocation], m_Pmech, omega},
                          sMode);
            m_Eft = ext->getOutput();
        }

        if ((pss != nullptr) && (pss->isEnabled())) {
            pss->timestep(time, inputs, sMode);
        }
        // compute the residuals

        genModel->timestep(time,
                           {inputs[voltageInLocation], inputs[angleInLocation], m_Eft, m_Pmech},
                           sMode);
        auto vals = genModel->getOutputs(
            {inputs[voltageInLocation], inputs[angleInLocation], m_Eft, m_Pmech},
            emptyStateData,
            cLocalSolverMode);
        P = vals[PoutLocation] * scale;
        Q = vals[QoutLocation] * scale;
    }
    // use this as the temporary state storage
    prevTime = time;
}

void DynamicGenerator::algebraicUpdate(const IOdata& inputs,
                                       const stateData& stateDataValue,
                                       double update[],
                                       const solverMode& sMode,
                                       double alpha)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) == 0) {
            return;
        }
        Generator::algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
        if (!opFlags[has_subobject_pflow_states]) {
            return;
        }
    }
    updateLocalCache(inputs, stateDataValue, sMode);

    // if ((!sD.empty ()) && (!isLocal (sMode)))
    // {
    for (auto* sub : getSubObjects()) {
        if (sub->isEnabled()) {
            sub->algebraicUpdate(
                subInputs.inputs[sub->locIndex], stateDataValue, update, sMode, alpha);
        }
    }
    // }
    // else
    // {
    //    stateData sD2 (0.0, m_state.data ());
    //    for (auto &sub : getSubObjects ())
    //    {
    //        if (sub->isEnabled ())
    //        {
    //            sub->algebraicUpdate (subInputs.inputs[sub->locIndex], sD2,
    //                                                                m_state.data (),
    //                                                                cLocalbSolverMode, alpha);
    //        }
    //    }
    // }
}

void DynamicGenerator::setFlag(std::string_view flag, bool val)
{
    if ((flag == "isoc") || (flag == "isochronous")) {
        opFlags.set(isochronous_operation, val);
        if (val) {
            if (isoc == nullptr) {
                add(new isocController(getName()));
                if (opFlags[dyn_initialized]) {
                    alert(isoc, UPDATE_REQUIRED);
                }
            } else {
                isoc->activate(prevTime);
            }
        }
        if (!val) {
            if (isoc != nullptr) {
                isoc->deactivate();
            }
        }
    } else {
        Generator::setFlag(flag, val);
    }
}

void DynamicGenerator::set(std::string_view param, double val, unit unitType)
{
    if (param.length() == 1) {
        switch (param.front()) {
            case 'r':
                m_Rs = val;
                if (genModel != nullptr) {
                    genModel->set(param, val, unitType);
                }
                break;
            case 'x':
                m_Xs = val;
                if (genModel != nullptr) {
                    genModel->set(param, val, unitType);
                }
                break;
            case 'h':
            case 'm':
            case 'd':
                if (genModel != nullptr) {
                    genModel->set(param, val, unitType);
                } else {
                    throw(unrecognizedParameter(param));
                }
                break;
            default:
                Generator::set(param, val, unitType);
        }
        return;
    }

    if (param == "xs") {
        m_Xs = val;
        if (genModel != nullptr) {
            genModel->set("xs", val);
        }
    } else if (param == "rs") {
        m_Rs = val;
        if (genModel != nullptr) {
            genModel->set("rs", val);
        }
    } else if (param == "eft") {
        m_Eft = val;
    } else if (param == "vref") {
        if (ext != nullptr) {
            ext->set(param, val, unitType);
        } else {
            m_Vtarget = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
        }
    } else if ((param == "rating") || (param == "base") || (param == "mbase")) {
        machineBasePower = convert(val, unitType, MVAR, systemBasePower, localBaseVoltage);
        opFlags.set(independent_machine_base);
        if (genModel != nullptr) {
            genModel->set("base", machineBasePower);
        }
    } else if (param == "basepower") {
        systemBasePower = convert(val, unitType, units::MW);
        if (opFlags[independent_machine_base]) {
        } else {
            machineBasePower = systemBasePower;
            for (auto* subobj : getSubObjects()) {
                subobj->set("basepower", machineBasePower);
            }
        }
    } else if ((param == "basefrequency") || (param == "basefreq")) {
        systemBaseFrequency = convert(val, unitType, rad / s);
        if (genModel != nullptr) {
            genModel->set(param, systemBaseFrequency);
        }
        if (gov != nullptr) {
            gov->set(param, systemBaseFrequency);
        }
    } else if (param == "pmax") {
        Pmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        if (machineBasePower < 0) {
            machineBasePower = convert(Pmax, puMW, MW, systemBasePower);
        }
        if (gov != nullptr) {
            gov->set(param, Pmax * systemBasePower / machineBasePower);
        }
    } else if (param == "pmin") {
        Pmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        if (gov != nullptr) {
            gov->set("pmin", Pmin * systemBasePower / machineBasePower);
        }
    } else {
        try {
            Generator::set(param, val, unitType);
        }
        catch (const unrecognizedParameter&) {
            for (auto* subobj : getSubObjects()) {
                subobj->setFlag("no_gridcomponent_set");
                try {
                    subobj->set(param, val, unitType);
                    subobj->setFlag("no_gridcomponent_set", false);
                    return;
                }
                catch (const unrecognizedParameter&) {
                    subobj->setFlag("no_gridcomponent_set", false);
                }
            }
            throw(unrecognizedParameter(param));
        }
    }
}

void DynamicGenerator::outputPartialDerivatives(const IOdata& inputs,
                                                const stateData& stateDataValue,
                                                matrixData<double>& matrixDataValue,
                                                const solverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) > 0) {
            Generator::outputPartialDerivatives(inputs, stateDataValue, matrixDataValue, sMode);
        }
        return;
    }
    const double scale = machineBasePower / systemBasePower;
    // matrixDataSparse<double> d;
    matrixDataScale<double> scaledMatrixData(matrixDataValue, scale);
    // compute the Jacobian

    genModel->outputPartialDerivatives(subInputs.inputs[genmodel_loc],
                                       stateDataValue,
                                       scaledMatrixData,
                                       sMode);
    // only valid locations are the generator internal coupled states
    genModel->ioPartialDerivatives(subInputs.inputs[genmodel_loc],
                                   stateDataValue,
                                   scaledMatrixData,
                                   subInputLocs.genModelInputLocsInternal,
                                   sMode);
}

count_t DynamicGenerator::outputDependencyCount(index_t num, const solverMode& sMode) const
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) > 0) {
            return Generator::outputDependencyCount(num, sMode);
        }
        return 0;
    }
    if (genModel != nullptr) {
        return 1 + genModel->outputDependencyCount(num, sMode);
    }
    if (stateSize(sMode) > 0) {
        return Generator::outputDependencyCount(num, sMode);
    }
    return 0;
}

void DynamicGenerator::ioPartialDerivatives(const IOdata& inputs,
                                            const stateData& stateDataValue,
                                            matrixData<double>& matrixDataValue,
                                            const IOlocs& inputLocs,
                                            const solverMode& sMode)
{
    if (isDynamic(sMode)) {
        const double scale = machineBasePower / systemBasePower;
        matrixDataScale<double> scaledMatrixData(matrixDataValue, scale);
        auto gmLocs = subInputLocs.genModelInputLocsExternal;
        gmLocs[voltageInLocation] = inputLocs[voltageInLocation];
        gmLocs[angleInLocation] = inputLocs[angleInLocation];
        genModel->ioPartialDerivatives(
            subInputs.inputs[genmodel_loc], stateDataValue, scaledMatrixData, gmLocs, sMode);
        return;
    }
    Generator::ioPartialDerivatives(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
}

IOdata DynamicGenerator::getOutputs(const IOdata& inputs,
                                    const stateData& stateDataValue,
                                    const solverMode& sMode) const
{
    if (isDynamic(sMode))  // use as a proxy for dynamic state
    {
        const double scale = machineBasePower / systemBasePower;
        auto output = genModel->getOutputs(subInputs.inputs[genmodel_loc], stateDataValue, sMode);
        output[PoutLocation] *= scale;
        output[QoutLocation] *= scale;
        return output;
    }
    return Generator::getOutputs(inputs, stateDataValue, sMode);
}

double DynamicGenerator::getRealPower(const IOdata& inputs,
                                      const stateData& stateDataValue,
                                      const solverMode& sMode) const
{
    if (isDynamic(sMode))  // use as a proxy for dynamic state
    {
        const double scale = machineBasePower / systemBasePower;
        const double output =
            genModel->getOutput(subInputs.inputs[genmodel_loc], stateDataValue, sMode, 0) * scale;
        // printf("t=%f (%s ) V=%f T=%f, P=%f\n", time, parent->name.c_str(),
        // inputs[voltageInLocation], inputs[angleInLocation], output[PoutLocation]);
        return output;
    }
    return Generator::getRealPower(inputs, stateDataValue, sMode);
}
double DynamicGenerator::getReactivePower(const IOdata& inputs,
                                          const stateData& stateDataValue,
                                          const solverMode& sMode) const
{
    if (isDynamic(sMode))  // use as a proxy for dynamic state
    {
        const double scale = machineBasePower / systemBasePower;
        const double output =
            genModel->getOutput(subInputs.inputs[genmodel_loc], stateDataValue, sMode, 1) * scale;
        return output;
    }
    return Generator::getReactivePower(inputs, stateDataValue, sMode);
}

// compute the residual for the dynamic states
void DynamicGenerator::residual(const IOdata& inputs,
                                const stateData& stateDataValue,
                                double resid[],
                                const solverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        Generator::residual(inputs, stateDataValue, resid, sMode);
        if (!opFlags[has_subobject_pflow_states]) {
            return;
        }
    }

    // compute the residuals
    updateLocalCache(inputs, stateDataValue, sMode);
    for (auto* sub : getSubObjects()) {
        if (sub->isEnabled()) {
            sub->residual(subInputs.inputs[sub->locIndex], stateDataValue, resid, sMode);
        }
    }
}

void DynamicGenerator::derivative(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  double deriv[],
                                  const solverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    // compute the residuals
    for (auto* sub : getSubObjects()) {
        if (sub->isEnabled()) {
            static_cast<gridSubModel*>(sub)->derivative(subInputs.inputs[sub->locIndex],
                                                        stateDataValue,
                                                        deriv,
                                                        sMode);
        }
    }
}

void DynamicGenerator::jacobianElements(const IOdata& inputs,
                                        const stateData& stateDataValue,
                                        matrixData<double>& matrixDataValue,
                                        const IOlocs& inputLocs,
                                        const solverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        Generator::jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
        if (!opFlags[has_subobject_pflow_states]) {
            return;
        }
    }

    updateLocalCache(inputs, stateDataValue, sMode);
    generateSubModelInputLocs(inputLocs, stateDataValue, sMode);

    // compute the Jacobian
    for (auto* sub : getSubObjects()) {
        if (sub->isEnabled()) {
            sub->jacobianElements(subInputs.inputs[sub->locIndex],
                                  stateDataValue,
                                  matrixDataValue,
                                  subInputLocs.inputLocs[sub->locIndex],
                                  sMode);
        }
    }
}

void DynamicGenerator::getStateName(stringVec& stNames,
                                    const solverMode& sMode,
                                    const std::string& prefix) const
{
    if ((!isDynamic(sMode)) && (stateSize(sMode) > 0)) {
        Generator::getStateName(stNames, sMode, prefix);
    }
    gridComponent::getStateName(stNames, sMode, prefix);  // NOLINT
}

void DynamicGenerator::rootTest(const IOdata& inputs,
                                const stateData& stateDataValue,
                                double roots[],
                                const solverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);

    for (auto* sub : getSubObjects()) {
        if (sub->checkFlag(has_roots)) {
            sub->rootTest(subInputs.inputs[sub->locIndex], stateDataValue, roots, sMode);
        }
    }
}

change_code DynamicGenerator::rootCheck(const IOdata& inputs,
                                        const stateData& stateDataValue,
                                        const solverMode& sMode,
                                        check_level_t level)
{
    auto ret = change_code::no_change;
    updateLocalCache(inputs, stateDataValue, sMode);

    for (auto* sub : getSubObjects()) {
        if (sub->checkFlag(has_alg_roots)) {
            const auto ret2 =
                sub->rootCheck(subInputs.inputs[sub->locIndex], stateDataValue, sMode, level);
            ret = std::max(ret2, ret);
        }
    }

    return ret;
}
void DynamicGenerator::rootTrigger(coreTime time,
                                   const IOdata& /*inputs*/,
                                   const std::vector<int>& rootMask,
                                   const solverMode& sMode)
{
    for (auto* sub : getSubObjects()) {
        if (sub->checkFlag(has_roots)) {
            sub->rootTrigger(time, subInputs.inputs[sub->locIndex], rootMask, sMode);
        }
    }
}

index_t DynamicGenerator::findIndex(std::string_view field, const solverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    for (auto* subobj : getSubObjects()) {
        ret = subobj->findIndex(field, sMode);
        if (ret != kInvalidLocation) {
            break;
        }
    }
    return ret;
}

CoreObject* DynamicGenerator::find(std::string_view object) const
{
    if (object == "genmodel") {
        return genModel;
    }
    if (object == "exciter") {
        return ext;
    }
    if ((object == "pset") || (object == "source")) {
        return pSetControl;
    }
    if (object == "vset") {
        return vSetControl;
    }
    if (object == "governor") {
        return gov;
    }
    if (object == "pss") {
        return pss;
    }
    if ((object == "isoc") || (object == "isoccontrol")) {
        return isoc;
    }
    return Generator::find(object);
}

CoreObject* DynamicGenerator::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "submodelcode")  // undocumented for internal use
    {
        for (auto* sub : getSubObjects()) {
            if (sub->locIndex == num) {
                return sub;
            }
        }
        return nullptr;
    }
    return gridComponent::getSubObject(typeName, num);
}

double DynamicGenerator::getFreq(const stateData& stateDataValue,
                                 const solverMode& sMode,
                                 index_t* freqOffset) const
{
    return genModel->getFreq(stateDataValue, sMode, freqOffset);
}

double DynamicGenerator::getAngle(const stateData& stateDataValue,
                                  const solverMode& sMode,
                                  index_t* angleOffset) const
{
    return genModel->getAngle(stateDataValue, sMode, angleOffset);
}

DynamicGenerator::subModelInputs::subModelInputs(): inputs(6)
{
    inputs[genmodel_loc].resize(4);
    inputs[exciter_loc].resize(3);
    inputs[governor_loc].resize(3);
}

DynamicGenerator::subModelInputLocs::subModelInputLocs():
    genModelInputLocsInternal(4), genModelInputLocsExternal(4), inputLocs(6)
{
    inputLocs[genmodel_loc].resize(4);
    inputLocs[exciter_loc].resize(3);
    inputLocs[governor_loc].resize(3);

    genModelInputLocsExternal[genModelEftInLocation] = kNullLocation;
    genModelInputLocsExternal[genModelPmechInLocation] = kNullLocation;
    genModelInputLocsInternal[voltageInLocation] = kNullLocation;
    genModelInputLocsInternal[angleInLocation] = kNullLocation;
}

void DynamicGenerator::generateSubModelInputs(const IOdata& inputs,
                                              const stateData& stateDataValue,
                                              const solverMode& sMode)
{
    if (!stateDataValue.updateRequired(subInputs.seqID)) {
        return;
    }
    if (inputs.empty()) {
        auto out = bus->getOutputs(noInputs, stateDataValue, sMode);
        subInputs.inputs[genmodel_loc][voltageInLocation] = out[voltageInLocation];
        subInputs.inputs[genmodel_loc][angleInLocation] = out[angleInLocation];
        subInputs.inputs[exciter_loc][exciterVoltageInLocation] = out[voltageInLocation];
        subInputs.inputs[governor_loc][govOmegaInLocation] = out[frequencyInLocation];
        if (isoc != nullptr) {
            subInputs.inputs[isoc_control][0] = out[frequencyInLocation] - 1.0;
        }
    } else {
        subInputs.inputs[genmodel_loc][voltageInLocation] = inputs[voltageInLocation];
        subInputs.inputs[genmodel_loc][angleInLocation] = inputs[angleInLocation];
        subInputs.inputs[exciter_loc][exciterVoltageInLocation] = inputs[voltageInLocation];
        if (inputs.size() > frequencyInLocation) {
            subInputs.inputs[governor_loc][govOmegaInLocation] = inputs[frequencyInLocation];
        }
        if (isoc != nullptr) {
            subInputs.inputs[isoc_control][0] = inputs[frequencyInLocation] - 1.0;
        }
    }
    if (!opFlags[uses_bus_frequency]) {
        subInputs.inputs[governor_loc][govOmegaInLocation] =
            genModel->getFreq(stateDataValue, sMode);
        if (isoc != nullptr) {
            subInputs.inputs[isoc_control][0] = genModel->getFreq(stateDataValue, sMode) - 1.0;
        }
    }

    const double scale = systemBasePower / machineBasePower;
    double Pcontrol = pSetControlUpdate(inputs, stateDataValue, sMode);
    Pcontrol = gmlc::utilities::valLimit(Pcontrol, Pmin, Pmax);

    subInputs.inputs[governor_loc][govpSetInLocation] = Pcontrol * scale;

    subInputs.inputs[exciter_loc][exciterVsetInLocation] =
        vSetControlUpdate(inputs, stateDataValue, sMode);
    double Eft = m_Eft;
    if ((ext != nullptr) && (ext->isEnabled())) {
        Eft = ext->getOutput(subInputs.inputs[exciter_loc], stateDataValue, sMode, 0);
    }
    subInputs.inputs[genmodel_loc][genModelEftInLocation] = Eft;
    double pmech = Pcontrol * scale;
    if ((gov != nullptr) && (gov->isEnabled())) {
        pmech = gov->getOutput(subInputs.inputs[governor_loc], stateDataValue, sMode, 0);
    }
    if (std::abs(pmech) > 1e25) {
        pmech = 0.0;
    }
    subInputs.inputs[genmodel_loc][genModelPmechInLocation] = pmech;

    if (!stateDataValue.empty()) {
        subInputs.seqID = stateDataValue.seqID;
    }
}

void DynamicGenerator::generateSubModelInputLocs(const IOlocs& inputLocs,
                                                 const stateData& stateDataValue,
                                                 const solverMode& sMode)
{
    if (!stateDataValue.updateRequired(subInputLocs.seqID)) {
        return;
    }

    subInputLocs.inputLocs[genmodel_loc][voltageInLocation] = inputLocs[voltageInLocation];
    subInputLocs.inputLocs[genmodel_loc][angleInLocation] = inputLocs[angleInLocation];
    subInputLocs.genModelInputLocsExternal[voltageInLocation] = inputLocs[voltageInLocation];
    subInputLocs.genModelInputLocsExternal[angleInLocation] = inputLocs[angleInLocation];

    if ((ext != nullptr) && (ext->isEnabled())) {
        subInputLocs.inputLocs[exciter_loc][exciterVoltageInLocation] =
            inputLocs[voltageInLocation];
        subInputLocs.inputLocs[exciter_loc][exciterVsetInLocation] = vSetLocation(sMode);
        subInputLocs.inputLocs[genmodel_loc][genModelEftInLocation] = ext->getOutputLoc(sMode, 0);
    } else {
        subInputLocs.inputLocs[genmodel_loc][genModelEftInLocation] = kNullLocation;
    }
    subInputLocs.genModelInputLocsInternal[genModelEftInLocation] =
        subInputLocs.inputLocs[genmodel_loc][genModelEftInLocation];
    if ((gov != nullptr) && (gov->isEnabled())) {
        if (genModel->checkFlag(uses_bus_frequency)) {
            subInputLocs.inputLocs[governor_loc][govOmegaInLocation] =
                inputLocs[frequencyInLocation];
        } else {
            index_t floc;
            genModel->getFreq(stateDataValue, sMode, &floc);
            subInputLocs.inputLocs[governor_loc][govOmegaInLocation] = floc;
        }
        subInputLocs.inputLocs[governor_loc][govpSetInLocation] = pSetLocation(sMode);
        subInputLocs.inputLocs[genmodel_loc][genModelPmechInLocation] = gov->getOutputLoc(sMode, 0);
    } else {
        subInputLocs.inputLocs[genmodel_loc][genModelPmechInLocation] = pSetLocation(sMode);
    }
    subInputLocs.genModelInputLocsInternal[genModelPmechInLocation] =
        subInputLocs.inputLocs[genmodel_loc][genModelPmechInLocation];

    if (isoc != nullptr) {
        subInputLocs.inputLocs[isoc_control][0] =
            subInputLocs.inputLocs[governor_loc][govOmegaInLocation];
    }
    subInputs.seqID = stateDataValue.seqID;
}

double DynamicGenerator::pSetControlUpdate(const IOdata& inputs,
                                           const stateData& stateDataValue,
                                           const solverMode& sMode)
{
    double val;
    if (pSetControl != nullptr) {
        val = pSetControl->getOutput(inputs, stateDataValue, sMode);
    } else {
        val = (!stateDataValue.empty()) ? (Pset + dPdt * (stateDataValue.time - prevTime)) : Pset;
    }
    if (opFlags[isochronous_operation]) {
        if (isoc != nullptr) {
            isoc->setLimits(Pmin - val, Pmax - val);
            isoc->setFreq(subInputs.inputs[isoc_control][0]);

            val = val + (isoc->getOutput() * machineBasePower / systemBasePower);
        }
    }
    return val;
}

double DynamicGenerator::vSetControlUpdate(const IOdata& inputs,
                                           const stateData& stateDataValue,
                                           const solverMode& sMode)
{
    return (vSetControl != nullptr) ? vSetControl->getOutput(inputs, stateDataValue, sMode) : 1.0;
}

index_t DynamicGenerator::pSetLocation(const solverMode& sMode)
{
    return (pSetControl != nullptr) ? pSetControl->getOutputLoc(sMode) : kNullLocation;
}
index_t DynamicGenerator::vSetLocation(const solverMode& sMode)
{
    return (vSetControl != nullptr) ? vSetControl->getOutputLoc(sMode) : kNullLocation;
}

}  // namespace griddyn
