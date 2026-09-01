/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DynamicGenerator.h"

#include "../GridBus.h"
#include "../Source.h"
#include "../Stabilizer.h"
#include "../controllers/Scheduler.h"
#include "../exciters/ExciterDC2A.h"
#include "../genmodels/otherGenModels.h"
#include "../governors/GovernorTypes.h"
#include "IsocController.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixDataCustomWriteOnly.hpp"
#include "utilities/MatrixDataScale.hpp"
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
static TypeFactory<DynamicGenerator>
    gGeneratorFactory("generator", std::to_array<std::string_view>({"local_dynamic"}));

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

DynamicGenerator::DynamicGenerator(DynModel dynModel, const std::string& objName):
    DynamicGenerator(objName)
{
    buildDynModel(dynModel);
}
CoreObject* DynamicGenerator::clone(CoreObject* obj) const
{
    auto* gen = cloneBaseFactory<DynamicGenerator, Generator>(this, obj, &gGeneratorFactory);
    if (gen == nullptr) {
        return obj;
    }
    gen->mechanicalPowerSourceExplicit = mechanicalPowerSourceExplicit;
    gen->mechanicalPowerOutput = mechanicalPowerOutput;
    gen->mechanicalPowerSourceName = mechanicalPowerSourceName;
    gen->mechanicalPowerSource =
        (mechanicalPowerSourceExplicit && (mechanicalPowerSource == gov)) ? gen->gov : nullptr;
    return gen;
}
namespace {
    constexpr index_t machineSignalLocationBase =
        kInvalidLocation - static_cast<index_t>(machineControllerSignalCount) - 1;

    index_t machineSignalLocation(index_t signalIndex)
    {
        return machineSignalLocationBase + signalIndex;
    }

    void copyMachineSignals(const IOdata& machineSignals, IOdata& exciterInputs)
    {
        for (index_t signalIndex = 0; signalIndex < machineControllerSignalCount; ++signalIndex) {
            exciterInputs[exciterMachineSignalBase + signalIndex] = machineSignals[signalIndex];
        }
    }

    void copyPssInputs(const IOdata& machineSignals,
                       double omega,
                       double voltage,
                       double mechanicalPower,
                       IOdata& pssInputs)
    {
        pssInputs[pssOmegaInLocation] = omega;
        pssInputs[pssVoltageInLocation] = voltage;
        pssInputs[pssPmechInLocation] = mechanicalPower;
        pssInputs[pssElectricalPowerInLocation] =
            machineSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE)];
    }

    const auto& getDynModelFromStringMap()
    {
        static const std::map<std::string_view, DynamicGenerator::DynModel, std::less<>>
            dynModelFromStringMap{
                {"typical", DynamicGenerator::DynModel::TYPICAL},
                {"simple", DynamicGenerator::DynModel::SIMPLE},
                {"model_only", DynamicGenerator::DynModel::MODEL_ONLY},
                {"modelonly", DynamicGenerator::DynModel::MODEL_ONLY},
                {"transient", DynamicGenerator::DynModel::TRANSIENT},
                {"subtransient", DynamicGenerator::DynModel::SUBTRANSIENT},
                {"detailed", DynamicGenerator::DynModel::DETAILED},
                {"none", DynamicGenerator::DynModel::NONE},
                {"dc", DynamicGenerator::DynModel::DC},
                {"renewable", DynamicGenerator::DynModel::RENEWABLE},
                {"variable", DynamicGenerator::DynModel::RENEWABLE},
            };
        return dynModelFromStringMap;
    }
}  // namespace

DynamicGenerator::DynModel DynamicGenerator::dynModelFromString(const std::string& dynModelType)
{
    const auto str = gmlc::utilities::convertToLowerCase(dynModelType);
    const auto& dynModelFromStringMap = getDynModelFromStringMap();
    const auto foundModel = dynModelFromStringMap.find(str);
    return (foundModel != dynModelFromStringMap.end()) ? foundModel->second : DynModel::INVALID;
}

void DynamicGenerator::buildDynModel(DynModel dynModel)
{
    switch (dynModel) {
        case DynModel::SIMPLE:
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
        case DynModel::DC:
            if (gov == nullptr) {
                add(new governors::GovernorIeeeSimple());
            }
            if (genModel == nullptr) {
                add(new genmodels::GenModelClassical());
            }

            break;
        case DynModel::TYPICAL:
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
        case DynModel::RENEWABLE:
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
        case DynModel::TRANSIENT:
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
        case DynModel::SUBTRANSIENT:
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
        case DynModel::DETAILED:
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
        case DynModel::MODEL_ONLY:
            if (genModel == nullptr) {
                add(new genmodels::GenModel4());
            }
            break;
        case DynModel::NONE:
            if (genModel == nullptr) {
                add(new GenModel());
            }
            break;
        case DynModel::INVALID:
        default:
            break;
    }
}

void DynamicGenerator::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (machineBasePower < 0) {
        machineBasePower = systemBasePower;
    }
    // automatically define a trivial generator model if none has been specified
    if (genModel == nullptr) {
        add(new GenModel());
    }
    resolveMechanicalPowerSource();
    if (gov != nullptr) {
        if (!genModel->checkFlag(GenModel::GenModelFlags::INTERNAL_FREQUENCY_CALCULATION)) {
            opFlags.set(USES_BUS_FREQUENCY);
        }
    }
    if (opFlags[ISOCHRONOUS_OPERATION]) {
        bus->setFlag("compute_frequency", true);
        // opFlags.set(uses_bus_frequency);
    }
    GridSecondary::dynObjectInitializeA(time0, flags);  // NOLINT
}

// initial conditions of dynamic states
void DynamicGenerator::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& desiredOutput,
                                            IOdata& fieldSet)
{
    Generator::dynObjectInitializeB(inputs, desiredOutput, fieldSet);

    // load the power set point
    if (opFlags[ISOCHRONOUS_OPERATION]) {
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

    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double theta = inputs[ANGLE_IN_LOCATION];

    modelInputs[VOLTAGE_IN_LOCATION] = voltage;
    modelInputs[ANGLE_IN_LOCATION] = theta;
    modelInputs[genModelPmechInLocation] = kNullVal;
    modelInputs[genModelEftInLocation] = kNullVal;

    localDesiredOutput[POUT_LOCATION] = P * scale;
    localDesiredOutput[QOUT_LOCATION] = Q * scale;

    IOdata computedFieldSet(4);
    genModel->dynInitializeB(modelInputs, localDesiredOutput, computedFieldSet);
    m_Pmech = computedFieldSet[genModelPmechInLocation];

    m_Eft = computedFieldSet[genModelEftInLocation];
    //  genModel->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);

    Pset = m_Pmech / scale;
    if (mechanicalPowerSourceExplicit && (mechanicalPowerSource != nullptr) &&
        (mechanicalPowerSource != gov)) {
        mechanicalPowerSource->setOutputInitializationTarget(mechanicalPowerOutput, m_Pmech);
    }
    if (isoc != nullptr) {
        Pset -= isoc->getOutput();
    }

    if ((ext != nullptr) && (ext->isEnabled())) {
        IOdata exciterInputs(exciterInputCount, 0.0);
        exciterInputs[exciterVoltageInLocation] = voltage;
        exciterInputs[exciterVsetInLocation] = 1.0;
        exciterInputs[exciterPmechInLocation] = m_Pmech;
        exciterInputs[exciterOmegaInLocation] = 1.0;
        modelInputs[genModelPmechInLocation] = m_Pmech;
        modelInputs[genModelEftInLocation] = m_Eft;
        copyMachineSignals(genModel->getMachineControllerSignals(modelInputs,
                                                                 emptyStateData,
                                                                 cLocalSolverMode),
                           exciterInputs);

        localDesiredOutput[0] = m_Eft;
        ext->dynInitializeB(exciterInputs, localDesiredOutput, computedFieldSet);

        //    ext->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
        // Vset=inputSetup[1];
    }
    if ((gov != nullptr) && (gov->isEnabled())) {
        const auto governorSignals =
            genModel->getMachineControllerSignals(modelInputs, emptyStateData, cLocalSolverMode);
        modelInputs[govOmegaInLocation] = 1.0;
        modelInputs[govpSetInLocation] = kNullVal;
        modelInputs[govElectricalPowerInLocation] =
            governorSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)];

        localDesiredOutput[0] = Pset * scale;
        if (isoc != nullptr) {
            localDesiredOutput[0] += isoc->getOutput() * scale;
        }
        gov->dynInitializeB(modelInputs, localDesiredOutput, computedFieldSet);

        //     gov->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
    }

    if ((pss != nullptr) && (pss->isEnabled())) {
        IOdata pssInputs(pssInputCount, 0.0);
        pssInputs[pssOmegaInLocation] = 1.0;
        pssInputs[pssVoltageInLocation] = voltage;
        pssInputs[pssPmechInLocation] = m_Pmech;
        pssInputs[pssElectricalPowerInLocation] = m_Pmech;
        localDesiredOutput[0] = 0;
        pss->dynInitializeB(pssInputs, localDesiredOutput, computedFieldSet);
        //    pss->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
    }

    modelInputs.resize(0);
    localDesiredOutput.resize(0);
    for (auto* sub : getSubObjects()) {
        // The machine, exciter, governor, and PSS above require their own
        // controller input contracts.  Do not initialize them again through
        // the generic empty-input path.
        if (sub->locIndex <= PSS_LOC) {
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
void DynamicGenerator::setState(CoreTime time,
                                const double state[],
                                const double dstateDt[],
                                const SolverMode& sMode)
{
    if (isDynamic(sMode)) {
        for (auto* subobj : getSubObjects()) {
            if (subobj->isEnabled()) {
                subobj->setState(time, state, dstateDt, sMode);
                // subobj->guessState (time, m_state.data (), m_dstate_dt.data (),
                // cLocalbSolverMode);
            }
        }
        Pset += dPdt * (time - prevTime);
        Pset = gmlc::utilities::valLimit(Pset, Pmin, Pmax);
        updateLocalCache(noInputs, emptyStateData, cLocalSolverMode);
    } else if (stateSize(sMode) > 0) {
        Generator::setState(time, state, dstateDt, sMode);
    }
    prevTime = time;
}

void DynamicGenerator::updateLocalCache(const IOdata& inputs,
                                        const StateData& stateDataValue,
                                        const SolverMode& sMode)
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
        P = -genModel->getOutput(subInputs.inputs[GEN_MODEL_LOC],
                                 stateDataValue,
                                 sMode,
                                 POUT_LOCATION) *
            scale;
        Q = -genModel->getOutput(subInputs.inputs[GEN_MODEL_LOC],
                                 stateDataValue,
                                 sMode,
                                 QOUT_LOCATION) *
            scale;
    }
}

// copy the current state to a vector
void DynamicGenerator::guessState(CoreTime time,
                                  double state[],
                                  double dstateDt[],
                                  const SolverMode& sMode)
{
    if (isDynamic(sMode)) {
        for (auto* subobj : getSubObjects()) {
            if (subobj->isEnabled()) {
                subobj->guessState(time, state, dstateDt, sMode);
                // subobj->guessState (time, m_state.data (), m_dstate_dt.data (),
                // cLocalbSolverMode);
            }
        }
    } else if (stateSize(sMode) > 0) {
        Generator::guessState(time, state, dstateDt, sMode);
    }
}

void DynamicGenerator::add(CoreObject* obj)
{
    Generator::add(obj);
}

void DynamicGenerator::add(GridSubModel* obj)
{
    if (dynamic_cast<Exciter*>(obj) != nullptr) {
        ext = static_cast<Exciter*>(replaceModel(obj, ext, EXCITER_LOC));
    } else if (dynamic_cast<GenModel*>(obj) != nullptr) {
        genModel = static_cast<GenModel*>(replaceModel(obj, genModel, GEN_MODEL_LOC));
        if (m_Rs != 0.0) {
            obj->set("rs", m_Rs);
        }
        if (m_Xs != 1.0) {
            obj->set("xs", m_Xs);
        }
    } else if (dynamic_cast<Governor*>(obj) != nullptr) {
        const bool reconnectExplicitSource =
            mechanicalPowerSourceExplicit && (mechanicalPowerSource == gov);
        gov = static_cast<Governor*>(replaceModel(obj, gov, GOVERNOR_LOC));
        if (reconnectExplicitSource) {
            setMechanicalPowerSource(gov, mechanicalPowerOutput);
        }
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
        pss = static_cast<Stabilizer*>(replaceModel(obj, pss, PSS_LOC));
    } else if (dynamic_cast<Source*>(obj) != nullptr) {
        auto* src = static_cast<Source*>(obj);
        if ((src->purpose_ == "power") || (src->purpose_ == "pset")) {
            pSetControl = static_cast<Source*>(replaceModel(obj, pSetControl, PSET_LOC));
            if (dynamic_cast<Scheduler*>(pSetControl) != nullptr) {
                sched = static_cast<Scheduler*>(pSetControl);
            }
        } else if ((src->purpose_ == "voltage") || (src->purpose_ == "vset")) {
            vSetControl = static_cast<Source*>(replaceModel(obj, vSetControl, VSET_LOC));
        } else if ((pSetControl == nullptr) && (src->purpose_.empty())) {
            pSetControl = static_cast<Source*>(replaceModel(obj, pSetControl, PSET_LOC));
        } else {
            throw(ObjectAddFailure(this));
        }
    } else if (dynamic_cast<IsocController*>(obj) != nullptr) {
        isoc = static_cast<IsocController*>(replaceModel(obj, isoc, ISOC_CONTROL_LOC));
        subInputLocs.inputLocs[ISOC_CONTROL_LOC].resize(1);
        subInputs.inputs[ISOC_CONTROL_LOC].resize(1);
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

void DynamicGenerator::setMechanicalPowerSource(GridSubModel* source, index_t outputIndex)
{
    if (source == nullptr) {
        clearMechanicalPowerSource();
        return;
    }
    if ((outputIndex < 0) || (outputIndex >= source->numOutputs())) {
        throw InvalidParameterValue("mechanical power output");
    }

    mechanicalPowerSource = source;
    mechanicalPowerOutput = outputIndex;
    mechanicalPowerSourceExplicit = true;
    mechanicalPowerSourceName =
        (source->getParent() != nullptr) ? fullObjectName(source) : source->getName();
    subInputs.seqID = 0;
    subInputLocs.seqID = 0;
}

void DynamicGenerator::setMechanicalPowerSource(std::string_view sourceName, index_t outputIndex)
{
    if ((sourceName.empty()) || (sourceName == "default") || (sourceName == "local")) {
        clearMechanicalPowerSource();
        return;
    }
    if (outputIndex < 0) {
        throw InvalidParameterValue("mechanical power output");
    }

    mechanicalPowerSource = nullptr;
    mechanicalPowerOutput = outputIndex;
    mechanicalPowerSourceExplicit = true;
    mechanicalPowerSourceName = sourceName;
    subInputs.seqID = 0;
    subInputLocs.seqID = 0;
}

void DynamicGenerator::clearMechanicalPowerSource()
{
    mechanicalPowerSource = nullptr;
    mechanicalPowerOutput = 0;
    mechanicalPowerSourceName.clear();
    mechanicalPowerSourceExplicit = false;
    subInputs.seqID = 0;
    subInputLocs.seqID = 0;
}

GridSubModel* DynamicGenerator::getMechanicalPowerSource() const
{
    return mechanicalPowerSourceExplicit ? mechanicalPowerSource : gov;
}

index_t DynamicGenerator::getMechanicalPowerOutput() const
{
    return mechanicalPowerSourceExplicit ? mechanicalPowerOutput : 0;
}

bool DynamicGenerator::hasExplicitMechanicalPowerSource() const
{
    return mechanicalPowerSourceExplicit;
}

void DynamicGenerator::resolveMechanicalPowerSource()
{
    if (!mechanicalPowerSourceExplicit || (mechanicalPowerSource != nullptr)) {
        return;
    }

    auto* source =
        dynamic_cast<GridSubModel*>(locateObject(mechanicalPowerSourceName, getRoot(), false));
    if (source == nullptr) {
        throw InvalidParameterValue("mechanical power source '" + mechanicalPowerSourceName + "'");
    }
    if (mechanicalPowerOutput >= source->numOutputs()) {
        throw InvalidParameterValue("mechanical power output");
    }
    mechanicalPowerSource = source;
}

GridSubModel* DynamicGenerator::replaceModel(GridSubModel* newObject,
                                             GridSubModel* oldObject,
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
        if (dmodel == DynModel::INVALID) {
            throw(InvalidParameterValue(val));
        }
        buildDynModel(dmodel);
    } else if ((param == "mechanical_power_source") || (param == "mechanicalpowersource") ||
               (param == "pmech_source") || (param == "pmechsource")) {
        setMechanicalPowerSource(val, mechanicalPowerOutput);
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

void DynamicGenerator::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    Generator::timestep(time, inputs, sMode);
    if (isDynamic(sMode)) {
        const double scale = machineBasePower / systemBasePower;
        const double omega = genModel->getFreq(emptyStateData, cLocalSolverMode);

        if ((gov != nullptr) && (gov->isEnabled())) {
            const IOdata governorMachineInputs{inputs[VOLTAGE_IN_LOCATION],
                                               inputs[ANGLE_IN_LOCATION],
                                               m_Eft,
                                               m_Pmech};
            const auto governorSignals =
                genModel->getMachineControllerSignals(governorMachineInputs,
                                                      emptyStateData,
                                                      cLocalSolverMode);
            gov->timestep(
                time,
                {omega,
                 Pset / scale,
                 governorSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)]},
                sMode);
        }
        auto* pmechSource = getMechanicalPowerSource();
        if ((pmechSource != nullptr) && (pmechSource->isEnabled())) {
            m_Pmech = pmechSource->getOutput(getMechanicalPowerOutput());
        }

        if ((pss != nullptr) && (pss->isEnabled())) {
            const IOdata genModelInputs{inputs[VOLTAGE_IN_LOCATION],
                                        inputs[ANGLE_IN_LOCATION],
                                        m_Eft,
                                        m_Pmech};
            IOdata pssInputs(pssInputCount, 0.0);
            copyPssInputs(genModel->getMachineControllerSignals(genModelInputs,
                                                                emptyStateData,
                                                                cLocalSolverMode),
                          omega,
                          inputs[VOLTAGE_IN_LOCATION],
                          m_Pmech,
                          pssInputs);
            pss->timestep(time, pssInputs, sMode);
        }

        if ((ext != nullptr) && (ext->isEnabled())) {
            const IOdata genModelInputs{inputs[VOLTAGE_IN_LOCATION],
                                        inputs[ANGLE_IN_LOCATION],
                                        m_Eft,
                                        m_Pmech};
            IOdata exciterInputs(exciterInputCount, 0.0);
            exciterInputs[exciterVoltageInLocation] = inputs[VOLTAGE_IN_LOCATION];
            exciterInputs[exciterVsetInLocation] = 1.0;
            exciterInputs[exciterPmechInLocation] = m_Pmech;
            exciterInputs[exciterOmegaInLocation] = omega;
            copyMachineSignals(genModel->getMachineControllerSignals(genModelInputs,
                                                                     emptyStateData,
                                                                     cLocalSolverMode),
                               exciterInputs);
            if ((pss != nullptr) && (pss->isEnabled()) && (pss->numOutputs() > 0)) {
                exciterInputs[exciterVssInLocation] = pss->getOutput();
            }
            ext->timestep(time, exciterInputs, sMode);
            m_Eft = ext->getOutput();
        }
        // compute the residuals

        genModel->timestep(time,
                           {inputs[VOLTAGE_IN_LOCATION], inputs[ANGLE_IN_LOCATION], m_Eft, m_Pmech},
                           sMode);
        auto vals = genModel->getOutputs(
            {inputs[VOLTAGE_IN_LOCATION], inputs[ANGLE_IN_LOCATION], m_Eft, m_Pmech},
            emptyStateData,
            cLocalSolverMode);
        P = vals[POUT_LOCATION] * scale;
        Q = vals[QOUT_LOCATION] * scale;
    }
    // use this as the temporary state storage
    prevTime = time;
}

void DynamicGenerator::algebraicUpdate(const IOdata& inputs,
                                       const StateData& stateDataValue,
                                       double update[],
                                       const SolverMode& sMode,
                                       double alpha)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) == 0) {
            return;
        }
        Generator::algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
        if (!opFlags[HAS_SUBOBJECT_PFLOW_STATES]) {
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
    //    StateData sD2 (0.0, m_state.data ());
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
        opFlags.set(ISOCHRONOUS_OPERATION, val);
        if (val) {
            if (isoc == nullptr) {
                add(new IsocController(getName()));
                if (opFlags[DYN_INITIALIZED]) {
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
                    throw(UnrecognizedParameter(param));
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
    } else if ((param == "mechanical_power_output") || (param == "mechanicalpoweroutput") ||
               (param == "pmech_output") || (param == "pmechoutput")) {
        const auto outputIndex = static_cast<index_t>(val);
        if ((val < 0.0) || (static_cast<double>(outputIndex) != val)) {
            throw InvalidParameterValue("mechanical power output");
        }
        if ((mechanicalPowerSource != nullptr) &&
            (outputIndex >= mechanicalPowerSource->numOutputs())) {
            throw InvalidParameterValue("mechanical power output");
        }
        mechanicalPowerOutput = outputIndex;
        subInputs.seqID = 0;
        subInputLocs.seqID = 0;
    } else if (param == "vref") {
        if (ext != nullptr) {
            ext->set(param, val, unitType);
        } else {
            m_Vtarget = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
        }
    } else if ((param == "rating") || (param == "base") || (param == "mbase")) {
        machineBasePower = convert(val, unitType, MVAR, systemBasePower, localBaseVoltage);
        opFlags.set(INDEPENDENT_MACHINE_BASE);
        if (genModel != nullptr) {
            genModel->set("base", machineBasePower);
        }
    } else if (param == "basepower") {
        systemBasePower = convert(val, unitType, units::MW);
        if (opFlags[INDEPENDENT_MACHINE_BASE]) {
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
        catch (const UnrecognizedParameter&) {
            for (auto* subobj : getSubObjects()) {
                subobj->setFlag("no_gridcomponent_set");
                try {
                    subobj->set(param, val, unitType);
                    subobj->setFlag("no_gridcomponent_set", false);
                    return;
                }
                catch (const UnrecognizedParameter&) {
                    subobj->setFlag("no_gridcomponent_set", false);
                }
            }
            throw(UnrecognizedParameter(param));
        }
    }
}

void DynamicGenerator::outputPartialDerivatives(const IOdata& inputs,
                                                const StateData& stateDataValue,
                                                MatrixData<double>& matrixDataValue,
                                                const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) > 0) {
            Generator::outputPartialDerivatives(inputs, stateDataValue, matrixDataValue, sMode);
        }
        return;
    }
    const double scale = machineBasePower / systemBasePower;
    // MatrixDataSparse<double> d;
    MatrixDataScale<double> scaledMatrixData(matrixDataValue, scale);
    // compute the Jacobian

    genModel->outputPartialDerivatives(subInputs.inputs[GEN_MODEL_LOC],
                                       stateDataValue,
                                       scaledMatrixData,
                                       sMode);
    // only valid locations are the generator internal coupled states
    genModel->ioPartialDerivatives(subInputs.inputs[GEN_MODEL_LOC],
                                   stateDataValue,
                                   scaledMatrixData,
                                   subInputLocs.genModelInputLocsInternal,
                                   sMode);
}

count_t DynamicGenerator::outputDependencyCount(index_t num, const SolverMode& sMode) const
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
                                            const StateData& stateDataValue,
                                            MatrixData<double>& matrixDataValue,
                                            const IOlocs& inputLocs,
                                            const SolverMode& sMode)
{
    if (isDynamic(sMode)) {
        const double scale = machineBasePower / systemBasePower;
        MatrixDataScale<double> scaledMatrixData(matrixDataValue, scale);
        auto gmLocs = subInputLocs.genModelInputLocsExternal;
        gmLocs[VOLTAGE_IN_LOCATION] = inputLocs[VOLTAGE_IN_LOCATION];
        gmLocs[ANGLE_IN_LOCATION] = inputLocs[ANGLE_IN_LOCATION];
        genModel->ioPartialDerivatives(
            subInputs.inputs[GEN_MODEL_LOC], stateDataValue, scaledMatrixData, gmLocs, sMode);
        return;
    }
    Generator::ioPartialDerivatives(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
}

IOdata DynamicGenerator::getOutputs(const IOdata& inputs,
                                    const StateData& stateDataValue,
                                    const SolverMode& sMode) const
{
    if (isDynamic(sMode))  // use as a proxy for dynamic state
    {
        const double scale = machineBasePower / systemBasePower;
        auto output = genModel->getOutputs(subInputs.inputs[GEN_MODEL_LOC], stateDataValue, sMode);
        output[POUT_LOCATION] *= scale;
        output[QOUT_LOCATION] *= scale;
        return output;
    }
    return Generator::getOutputs(inputs, stateDataValue, sMode);
}

double DynamicGenerator::getRealPower(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      const SolverMode& sMode) const
{
    if (isDynamic(sMode))  // use as a proxy for dynamic state
    {
        const double scale = machineBasePower / systemBasePower;
        const double output =
            genModel->getOutput(subInputs.inputs[GEN_MODEL_LOC], stateDataValue, sMode, 0) * scale;
        // printf("t=%f (%s ) V=%f T=%f, P=%f\n", time, parent->name.c_str(),
        // inputs[VOLTAGE_IN_LOCATION], inputs[ANGLE_IN_LOCATION], output[POUT_LOCATION]);
        return output;
    }
    return Generator::getRealPower(inputs, stateDataValue, sMode);
}
double DynamicGenerator::getReactivePower(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          const SolverMode& sMode) const
{
    if (isDynamic(sMode))  // use as a proxy for dynamic state
    {
        const double scale = machineBasePower / systemBasePower;
        const double output =
            genModel->getOutput(subInputs.inputs[GEN_MODEL_LOC], stateDataValue, sMode, 1) * scale;
        return output;
    }
    return Generator::getReactivePower(inputs, stateDataValue, sMode);
}

// compute the residual for the dynamic states
void DynamicGenerator::residual(const IOdata& inputs,
                                const StateData& stateDataValue,
                                double resid[],
                                const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        Generator::residual(inputs, stateDataValue, resid, sMode);
        if (!opFlags[HAS_SUBOBJECT_PFLOW_STATES]) {
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
                                  const StateData& stateDataValue,
                                  double deriv[],
                                  const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    // compute the residuals
    for (auto* sub : getSubObjects()) {
        if (sub->isEnabled()) {
            static_cast<GridSubModel*>(sub)->derivative(subInputs.inputs[sub->locIndex],
                                                        stateDataValue,
                                                        deriv,
                                                        sMode);
        }
    }
}

void DynamicGenerator::jacobianElements(const IOdata& inputs,
                                        const StateData& stateDataValue,
                                        MatrixData<double>& matrixDataValue,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        Generator::jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
        if (!opFlags[HAS_SUBOBJECT_PFLOW_STATES]) {
            return;
        }
    }

    updateLocalCache(inputs, stateDataValue, sMode);
    generateSubModelInputLocs(inputLocs, stateDataValue, sMode);

    auto machineSignalInputLocations = subInputLocs.genModelInputLocsInternal;
    machineSignalInputLocations[VOLTAGE_IN_LOCATION] =
        subInputLocs.genModelInputLocsExternal[VOLTAGE_IN_LOCATION];
    machineSignalInputLocations[ANGLE_IN_LOCATION] =
        subInputLocs.genModelInputLocsExternal[ANGLE_IN_LOCATION];
    const auto machineSignalDerivatives =
        genModel->getMachineControllerSignalDerivatives(subInputs.inputs[GEN_MODEL_LOC],
                                                        stateDataValue,
                                                        machineSignalInputLocations,
                                                        sMode);

    // compute the Jacobian
    for (auto* sub : getSubObjects()) {
        if (sub->isEnabled()) {
            if (((sub == ext) && (ext->numInputs() > exciterMachineSignalBase)) || (sub == pss) ||
                ((sub == gov) && (gov->numInputs() > govElectricalPowerInLocation))) {
                MatrixDataCustomWriteOnly<double> translatedMatrix;
                translatedMatrix.setFunction(
                    [&matrixDataValue,
                     &machineSignalDerivatives](index_t row, index_t column, double value) {
                        if ((column >= machineSignalLocationBase) &&
                            (column < machineSignalLocationBase +
                                 static_cast<index_t>(machineControllerSignalCount))) {
                            const index_t signalIndex = column - machineSignalLocationBase;
                            for (const auto& derivative : machineSignalDerivatives[signalIndex]) {
                                matrixDataValue.assign(row,
                                                       derivative.location,
                                                       value * derivative.value);
                            }
                            return;
                        }
                        matrixDataValue.assign(row, column, value);
                    });
                sub->jacobianElements(subInputs.inputs[sub->locIndex],
                                      stateDataValue,
                                      translatedMatrix,
                                      subInputLocs.inputLocs[sub->locIndex],
                                      sMode);
                continue;
            }
            sub->jacobianElements(subInputs.inputs[sub->locIndex],
                                  stateDataValue,
                                  matrixDataValue,
                                  subInputLocs.inputLocs[sub->locIndex],
                                  sMode);
        }
    }
}

void DynamicGenerator::getStateName(stringVec& stNames,
                                    const SolverMode& sMode,
                                    const std::string& prefix) const
{
    if ((!isDynamic(sMode)) && (stateSize(sMode) > 0)) {
        Generator::getStateName(stNames, sMode, prefix);
    }
    GridComponent::getStateName(stNames, sMode, prefix);  // NOLINT
}

void DynamicGenerator::rootTest(const IOdata& inputs,
                                const StateData& stateDataValue,
                                double roots[],
                                const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);

    for (auto* sub : getSubObjects()) {
        if (sub->checkFlag(HAS_ROOTS)) {
            sub->rootTest(subInputs.inputs[sub->locIndex], stateDataValue, roots, sMode);
        }
    }
}

ChangeCode DynamicGenerator::rootCheck(const IOdata& inputs,
                                       const StateData& stateDataValue,
                                       const SolverMode& sMode,
                                       CheckLevel level)
{
    auto ret = ChangeCode::NO_CHANGE;
    updateLocalCache(inputs, stateDataValue, sMode);

    for (auto* sub : getSubObjects()) {
        if (sub->checkFlag(HAS_ALG_ROOTS)) {
            const auto ret2 =
                sub->rootCheck(subInputs.inputs[sub->locIndex], stateDataValue, sMode, level);
            ret = std::max(ret2, ret);
        }
    }

    return ret;
}
void DynamicGenerator::rootTrigger(CoreTime time,
                                   const IOdata& /*inputs*/,
                                   const std::vector<int>& rootMask,
                                   const SolverMode& sMode)
{
    for (auto* sub : getSubObjects()) {
        if (sub->checkFlag(HAS_ROOTS)) {
            sub->rootTrigger(time, subInputs.inputs[sub->locIndex], rootMask, sMode);
        }
    }
}

index_t DynamicGenerator::findIndex(std::string_view field, const SolverMode& sMode) const
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
    return GridComponent::getSubObject(typeName, num);
}

double DynamicGenerator::getFreq(const StateData& stateDataValue,
                                 const SolverMode& sMode,
                                 index_t* freqOffset) const
{
    return genModel->getFreq(stateDataValue, sMode, freqOffset);
}

double DynamicGenerator::getAngle(const StateData& stateDataValue,
                                  const SolverMode& sMode,
                                  index_t* angleOffset) const
{
    return genModel->getAngle(stateDataValue, sMode, angleOffset);
}

DynamicGenerator::SubModelInputs::SubModelInputs(): inputs(6)
{
    inputs[GEN_MODEL_LOC].resize(4);
    inputs[EXCITER_LOC].resize(exciterInputCount);
    inputs[GOVERNOR_LOC].resize(3);
    inputs[PSS_LOC].resize(pssInputCount);
}

DynamicGenerator::SubModelInputLocs::SubModelInputLocs():
    genModelInputLocsInternal(4), genModelInputLocsExternal(4), inputLocs(6)
{
    inputLocs[GEN_MODEL_LOC].resize(4);
    inputLocs[EXCITER_LOC].resize(exciterInputCount);
    inputLocs[GOVERNOR_LOC].resize(3);
    inputLocs[PSS_LOC].resize(pssInputCount);

    genModelInputLocsExternal[genModelEftInLocation] = kNullLocation;
    genModelInputLocsExternal[genModelPmechInLocation] = kNullLocation;
    genModelInputLocsInternal[VOLTAGE_IN_LOCATION] = kNullLocation;
    genModelInputLocsInternal[ANGLE_IN_LOCATION] = kNullLocation;
}

void DynamicGenerator::generateSubModelInputs(const IOdata& inputs,
                                              const StateData& stateDataValue,
                                              const SolverMode& sMode)
{
    if (!stateDataValue.updateRequired(subInputs.seqID)) {
        return;
    }
    if (inputs.empty()) {
        auto out = bus->getOutputs(noInputs, stateDataValue, sMode);
        subInputs.inputs[GEN_MODEL_LOC][VOLTAGE_IN_LOCATION] = out[VOLTAGE_IN_LOCATION];
        subInputs.inputs[GEN_MODEL_LOC][ANGLE_IN_LOCATION] = out[ANGLE_IN_LOCATION];
        subInputs.inputs[EXCITER_LOC][exciterVoltageInLocation] = out[VOLTAGE_IN_LOCATION];
        subInputs.inputs[GOVERNOR_LOC][govOmegaInLocation] = out[FREQUENCY_IN_LOCATION];
        if (isoc != nullptr) {
            subInputs.inputs[ISOC_CONTROL_LOC][0] = out[FREQUENCY_IN_LOCATION] - 1.0;
        }
    } else {
        subInputs.inputs[GEN_MODEL_LOC][VOLTAGE_IN_LOCATION] = inputs[VOLTAGE_IN_LOCATION];
        subInputs.inputs[GEN_MODEL_LOC][ANGLE_IN_LOCATION] = inputs[ANGLE_IN_LOCATION];
        subInputs.inputs[EXCITER_LOC][exciterVoltageInLocation] = inputs[VOLTAGE_IN_LOCATION];
        if (inputs.size() > FREQUENCY_IN_LOCATION) {
            subInputs.inputs[GOVERNOR_LOC][govOmegaInLocation] = inputs[FREQUENCY_IN_LOCATION];
        }
        if (isoc != nullptr) {
            subInputs.inputs[ISOC_CONTROL_LOC][0] = inputs[FREQUENCY_IN_LOCATION] - 1.0;
        }
    }
    if (!opFlags[USES_BUS_FREQUENCY]) {
        subInputs.inputs[GOVERNOR_LOC][govOmegaInLocation] =
            genModel->getFreq(stateDataValue, sMode);
        if (isoc != nullptr) {
            subInputs.inputs[ISOC_CONTROL_LOC][0] = genModel->getFreq(stateDataValue, sMode) - 1.0;
        }
    }
    subInputs.inputs[EXCITER_LOC][exciterOmegaInLocation] =
        subInputs.inputs[GOVERNOR_LOC][govOmegaInLocation];

    const double scale = systemBasePower / machineBasePower;
    double pcontrol = pSetControlUpdate(inputs, stateDataValue, sMode);
    pcontrol = gmlc::utilities::valLimit(pcontrol, Pmin, Pmax);

    subInputs.inputs[GOVERNOR_LOC][govpSetInLocation] = pcontrol * scale;

    double pmech = pcontrol * scale;
    auto* pmechSource = getMechanicalPowerSource();
    if ((pmechSource != nullptr) && (pmechSource->isEnabled())) {
        const auto& sourceInputs = (pmechSource == gov) ? subInputs.inputs[GOVERNOR_LOC] : noInputs;
        pmech =
            pmechSource->getOutput(sourceInputs, stateDataValue, sMode, getMechanicalPowerOutput());
    }
    if (std::abs(pmech) > 1e25) {
        pmech = 0.0;
    }
    subInputs.inputs[GEN_MODEL_LOC][genModelPmechInLocation] = pmech;
    subInputs.inputs[EXCITER_LOC][exciterPmechInLocation] = pmech;
    subInputs.inputs[EXCITER_LOC][exciterVsetInLocation] =
        vSetControlUpdate(inputs, stateDataValue, sMode);
    const auto machineSignals =
        genModel->getMachineControllerSignals(subInputs.inputs[GEN_MODEL_LOC],
                                              stateDataValue,
                                              sMode);
    subInputs.inputs[GOVERNOR_LOC][govElectricalPowerInLocation] =
        machineSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)];
    copyMachineSignals(machineSignals, subInputs.inputs[EXCITER_LOC]);
    if ((pss != nullptr) && (pss->isEnabled())) {
        copyPssInputs(machineSignals,
                      subInputs.inputs[GOVERNOR_LOC][govOmegaInLocation],
                      subInputs.inputs[EXCITER_LOC][exciterVoltageInLocation],
                      pmech,
                      subInputs.inputs[PSS_LOC]);
    }
    subInputs.inputs[EXCITER_LOC][exciterVssInLocation] = 0.0;
    if ((pss != nullptr) && (pss->isEnabled()) && (pss->numOutputs() > 0)) {
        subInputs.inputs[EXCITER_LOC][exciterVssInLocation] =
            pss->getOutput(subInputs.inputs[PSS_LOC], stateDataValue, sMode);
    }
    double eft = m_Eft;
    if ((ext != nullptr) && (ext->isEnabled())) {
        eft = ext->getOutput(subInputs.inputs[EXCITER_LOC], stateDataValue, sMode, 0);
    }
    subInputs.inputs[GEN_MODEL_LOC][genModelEftInLocation] = eft;

    if (!stateDataValue.empty()) {
        subInputs.seqID = stateDataValue.seqID;
    }
}

void DynamicGenerator::generateSubModelInputLocs(const IOlocs& inputLocs,
                                                 const StateData& stateDataValue,
                                                 const SolverMode& sMode)
{
    if (!stateDataValue.updateRequired(subInputLocs.seqID)) {
        return;
    }

    subInputLocs.inputLocs[GEN_MODEL_LOC][VOLTAGE_IN_LOCATION] = inputLocs[VOLTAGE_IN_LOCATION];
    subInputLocs.inputLocs[GEN_MODEL_LOC][ANGLE_IN_LOCATION] = inputLocs[ANGLE_IN_LOCATION];
    subInputLocs.genModelInputLocsExternal[VOLTAGE_IN_LOCATION] = inputLocs[VOLTAGE_IN_LOCATION];
    subInputLocs.genModelInputLocsExternal[ANGLE_IN_LOCATION] = inputLocs[ANGLE_IN_LOCATION];

    if ((ext != nullptr) && (ext->isEnabled())) {
        subInputLocs.inputLocs[EXCITER_LOC][exciterVoltageInLocation] =
            inputLocs[VOLTAGE_IN_LOCATION];
        subInputLocs.inputLocs[EXCITER_LOC][exciterVsetInLocation] = vSetLocation(sMode);
        if (ext->numInputs() > exciterMachineSignalBase) {
            for (index_t signalIndex = 0; signalIndex < machineControllerSignalCount;
                 ++signalIndex) {
                subInputLocs.inputLocs[EXCITER_LOC][exciterMachineSignalBase + signalIndex] =
                    machineSignalLocation(signalIndex);
            }
            subInputLocs.inputLocs[EXCITER_LOC][exciterVssInLocation] =
                ((pss != nullptr) && (pss->isEnabled()) && (pss->numOutputs() > 0)) ?
                pss->getOutputLoc(sMode, 0) :
                kNullLocation;
        }
        subInputLocs.inputLocs[GEN_MODEL_LOC][genModelEftInLocation] = ext->getOutputLoc(sMode, 0);
    } else {
        subInputLocs.inputLocs[GEN_MODEL_LOC][genModelEftInLocation] = kNullLocation;
    }
    subInputLocs.genModelInputLocsInternal[genModelEftInLocation] =
        subInputLocs.inputLocs[GEN_MODEL_LOC][genModelEftInLocation];
    if ((gov != nullptr) && (gov->isEnabled())) {
        if (genModel->checkFlag(USES_BUS_FREQUENCY)) {
            subInputLocs.inputLocs[GOVERNOR_LOC][govOmegaInLocation] =
                inputLocs[FREQUENCY_IN_LOCATION];
        } else {
            index_t floc;
            genModel->getFreq(stateDataValue, sMode, &floc);
            subInputLocs.inputLocs[GOVERNOR_LOC][govOmegaInLocation] = floc;
        }
        subInputLocs.inputLocs[GOVERNOR_LOC][govpSetInLocation] = pSetLocation(sMode);
        subInputLocs.inputLocs[GOVERNOR_LOC][govElectricalPowerInLocation] =
            machineSignalLocation(static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER));
    }

    auto* pmechSource = getMechanicalPowerSource();
    if ((pmechSource != nullptr) && (pmechSource->isEnabled())) {
        subInputLocs.inputLocs[GEN_MODEL_LOC][genModelPmechInLocation] =
            pmechSource->getOutputLoc(sMode, getMechanicalPowerOutput());
    } else {
        subInputLocs.inputLocs[GEN_MODEL_LOC][genModelPmechInLocation] = pSetLocation(sMode);
    }
    subInputLocs.genModelInputLocsInternal[genModelPmechInLocation] =
        subInputLocs.inputLocs[GEN_MODEL_LOC][genModelPmechInLocation];

    if ((pss != nullptr) && (pss->isEnabled())) {
        index_t omegaLocation;
        genModel->getFreq(stateDataValue, sMode, &omegaLocation);
        subInputLocs.inputLocs[PSS_LOC][pssOmegaInLocation] = omegaLocation;
        subInputLocs.inputLocs[PSS_LOC][pssVoltageInLocation] = inputLocs[VOLTAGE_IN_LOCATION];
        subInputLocs.inputLocs[PSS_LOC][pssPmechInLocation] =
            subInputLocs.inputLocs[GEN_MODEL_LOC][genModelPmechInLocation];
        subInputLocs.inputLocs[PSS_LOC][pssElectricalPowerInLocation] =
            machineSignalLocation(static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE));
    }

    if (isoc != nullptr) {
        subInputLocs.inputLocs[ISOC_CONTROL_LOC][0] =
            subInputLocs.inputLocs[GOVERNOR_LOC][govOmegaInLocation];
    }
    // Input locations differ between solver modes even when the state sequence
    // ID is unchanged, so leave subInputLocs uncached and recompute them.
    subInputs.seqID = stateDataValue.seqID;
}

double DynamicGenerator::pSetControlUpdate(const IOdata& inputs,
                                           const StateData& stateDataValue,
                                           const SolverMode& sMode)
{
    double val;
    if (pSetControl != nullptr) {
        val = pSetControl->getOutput(inputs, stateDataValue, sMode);
    } else {
        val = (!stateDataValue.empty()) ? (Pset + dPdt * (stateDataValue.time - prevTime)) : Pset;
    }
    if (opFlags[ISOCHRONOUS_OPERATION]) {
        if (isoc != nullptr) {
            isoc->setLimits(Pmin - val, Pmax - val);
            isoc->setFreq(subInputs.inputs[ISOC_CONTROL_LOC][0]);

            val = val + (isoc->getOutput() * machineBasePower / systemBasePower);
        }
    }
    return val;
}

double DynamicGenerator::vSetControlUpdate(const IOdata& inputs,
                                           const StateData& stateDataValue,
                                           const SolverMode& sMode)
{
    return (vSetControl != nullptr) ? vSetControl->getOutput(inputs, stateDataValue, sMode) : 1.0;
}

index_t DynamicGenerator::pSetLocation(const SolverMode& sMode)
{
    return (pSetControl != nullptr) ? pSetControl->getOutputLoc(sMode) : kNullLocation;
}
index_t DynamicGenerator::vSetLocation(const SolverMode& sMode)
{
    return (vSetControl != nullptr) ? vSetControl->getOutputLoc(sMode) : kNullLocation;
}

}  // namespace griddyn
