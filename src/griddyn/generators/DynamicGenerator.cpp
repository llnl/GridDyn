/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DynamicGenerator.h"

#include "../ExcitationLimiter.h"
#include "../GridBus.h"
#include "../Source.h"
#include "../Stabilizer.h"
#include "../VoltageCompensator.h"
#include "../controllers/Scheduler.h"
#include "../exciters/ExciterDC2A.h"
#include "../genmodels/otherGenModels.h"
#include "../governors/GovernorTypes.h"
#include "../voltagecompensators/VoltageCompensatorIeeeVC.h"
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
static TypeFactory<VoltageCompensator>
    gVoltageCompensatorFactory("voltagecompensator", std::to_array<std::string_view>({"vcomp"}));
static ChildTypeFactory<voltagecompensators::VoltageCompensatorIeeeVC, VoltageCompensator>
    gVoltageCompensatorIeeeVCFactory("voltagecompensator", "ieeevc");

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
    gen->voltageCompensator = (voltageCompensator != nullptr) ?
        dynamic_cast<VoltageCompensator*>(gen->find("voltagecompensator")) :
        nullptr;
    gen->oel = nullptr;
    gen->uel = nullptr;
    for (auto* sub : gen->getSubObjects()) {
        if (sub->locIndex == OEL_LOC) {
            gen->oel = dynamic_cast<ExcitationLimiter*>(sub);
        } else if (sub->locIndex == UEL_LOC) {
            gen->uel = dynamic_cast<ExcitationLimiter*>(sub);
        }
    }
    gen->resetSignalRoutesForDynamicInitialization();
    return gen;
}
namespace {
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
    if (!signalRoutesReady) {
        compileSignalRoutes();
    }
}

void DynamicGenerator::resetSignalRoutesForDynamicInitialization()
{
    for (auto& routes : signalRoutes) {
        routes.clear();
    }
    for (auto& locations : routeInputLocations) {
        locations = {};
    }
    signalFrame = {};
    boundStabilizer = nullptr;
    boundMechanicalPowerSource = nullptr;
    mechanicalPowerWasInvalid = false;
    signalRoutesReady = false;
    subInputs = SubModelInputs{};
    subInputLocs = SubModelInputLocs{};
}

void DynamicGenerator::compileSignalRoutes()
{
    for (auto& routes : signalRoutes) {
        routes.clear();
    }
    boundStabilizer = ((pss != nullptr) && (pss->numOutputs() > 0)) ? pss : nullptr;
    boundMechanicalPowerSource = getMechanicalPowerSource();
    if ((oel != nullptr && oel->role() != ExcitationLimiter::Role::OVER) ||
        (uel != nullptr && uel->role() != ExcitationLimiter::Role::UNDER)) {
        throw InvalidParameterValue("excitation limiter role changed after attachment");
    }
    if ((oel != nullptr &&
         (ext == nullptr || !ext->supportsLimiterSignal(ExciterLimiterSignal::OVER))) ||
        (uel != nullptr &&
         (ext == nullptr || !ext->supportsLimiterSignal(ExciterLimiterSignal::UNDER)))) {
        throw InvalidParameterValue("exciter does not support the attached OEL/UEL action");
    }
    signalFrame.values[stabilizerOutput] = 0.0;
    signalFrame.values[overExcitationAction] = 0.0;
    signalFrame.values[underExcitationAction] = 0.0;
    auto bind =
        [this](SubModelLocations model, index_t input, index_t source, std::string sourceName) {
            signalRoutes[model].addInput(input, source, std::move(sourceName));
        };

    bind(GEN_MODEL_LOC, VOLTAGE_IN_LOCATION, terminalVoltage, "terminal voltage");
    bind(GEN_MODEL_LOC, ANGLE_IN_LOCATION, terminalAngle, "terminal angle");
    bind(GEN_MODEL_LOC, genModelEftInLocation, fieldVoltage, "field voltage");
    bind(GEN_MODEL_LOC, genModelPmechInLocation, mechanicalPower, "mechanical power");

    bind(GOVERNOR_LOC, govOmegaInLocation, controllerOmega, "controller frequency");
    bind(GOVERNOR_LOC, govpSetInLocation, activePowerCommand, "active power command");
    bind(GOVERNOR_LOC,
         govElectricalPowerInLocation,
         governorElectricalPower,
         "machine electrical power");

    bind(PSS_LOC, pssOmegaInLocation, controllerOmega, "controller frequency");
    bind(PSS_LOC, pssVoltageInLocation, exciterVoltage, "exciter sensed voltage");
    bind(PSS_LOC, pssPmechInLocation, mechanicalPower, "mechanical power");
    bind(PSS_LOC,
         pssElectricalPowerInLocation,
         machineSignalBase + static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE),
         "machine electrical torque");

    bind(EXCITER_LOC, exciterVoltageInLocation, exciterVoltage, "exciter sensed voltage");
    bind(EXCITER_LOC, exciterVsetInLocation, voltageSetpoint, "voltage setpoint");
    bind(EXCITER_LOC, exciterPmechInLocation, mechanicalPower, "mechanical power");
    bind(EXCITER_LOC, exciterOmegaInLocation, controllerOmega, "controller frequency");
    bind(EXCITER_LOC,
         exciterVssInLocation,
         stabilizerOutput,
         (boundStabilizer != nullptr) ? boundStabilizer->getName() : "neutral stabilizer");
    bind(EXCITER_LOC,
         exciterVuelInLocation,
         underExcitationAction,
         (uel != nullptr) ? uel->getName() : "neutral UEL");
    bind(EXCITER_LOC,
         exciterVoelInLocation,
         overExcitationAction,
         (oel != nullptr) ? oel->getName() : "neutral OEL");

    bind(VOLTAGE_COMPENSATOR_LOC,
         voltageCompensatorVoltageInLocation,
         terminalVoltage,
         "terminal voltage");
    for (index_t signalIndex = 0; signalIndex < machineControllerSignalCount; ++signalIndex) {
        const auto signal = machineSignalBase + signalIndex;
        const auto sourceName = "machine signal " + std::to_string(signalIndex);
        bind(EXCITER_LOC, exciterMachineSignalBase + signalIndex, signal, sourceName);
        bind(VOLTAGE_COMPENSATOR_LOC,
             voltageCompensatorMachineSignalBase + signalIndex,
             signal,
             sourceName);
    }
    bind(ISOC_CONTROL_LOC, 0, isochronousFrequency, "isochronous frequency error");
    for (auto model : {OEL_LOC, UEL_LOC}) {
        bind(model,
             limiterFieldCurrentInLocation,
             machineSignalBase + static_cast<index_t>(MachineControllerSignal::XADIFD),
             "machine field current");
        bind(model,
             limiterIdInLocation,
             machineSignalBase + static_cast<index_t>(MachineControllerSignal::ID),
             "machine Id");
        bind(model,
             limiterIqInLocation,
             machineSignalBase + static_cast<index_t>(MachineControllerSignal::IQ),
             "machine Iq");
        bind(model,
             limiterVdInLocation,
             machineSignalBase + static_cast<index_t>(MachineControllerSignal::VD),
             "machine Vd");
        bind(model,
             limiterVqInLocation,
             machineSignalBase + static_cast<index_t>(MachineControllerSignal::VQ),
             "machine Vq");
    }
    signalRoutesReady = true;
}

void DynamicGenerator::writeModelInputs(SubModelLocations model,
                                        const StateData& stateDataValue,
                                        const SolverMode& sMode)
{
    if (!signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator signal routes require initialization");
    }
    signalRoutes[model].writeValues({.hostInputs = signalFrame.values,
                                     .hostInputLocs = nullptr,
                                     .stateData = stateDataValue,
                                     .solverMode = sMode},
                                    subInputs.inputs[model]);
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

    // Controller signals such as XadIfd for reduced-order machines use the
    // initialized field voltage and mechanical power inputs.  Populate these
    // before taking the shared signal snapshot used by the governor, exciter,
    // and voltage compensator.
    modelInputs[genModelPmechInLocation] = m_Pmech;
    modelInputs[genModelEftInLocation] = m_Eft;

    const auto machineSignals =
        genModel->getMachineControllerSignals(modelInputs, emptyStateData, cLocalSolverMode);
    signalFrame.values[terminalVoltage] = voltage;
    signalFrame.values[terminalAngle] = theta;
    signalFrame.values[controllerOmega] = 1.0;
    signalFrame.values[mechanicalPower] = m_Pmech;
    signalFrame.values[fieldVoltage] = m_Eft;
    signalFrame.values[activePowerCommand] = kNullVal;
    signalFrame.values[voltageSetpoint] = 1.0;
    signalFrame.values[exciterVoltage] = voltage;
    signalFrame.values[stabilizerOutput] = 0.0;
    for (index_t signalIndex = 0; signalIndex < machineControllerSignalCount; ++signalIndex) {
        signalFrame.values[machineSignalBase + signalIndex] = machineSignals[signalIndex];
    }
    signalFrame.values[governorElectricalPower] =
        machineSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)];
    signalFrame.values[overExcitationAction] = 0.0;
    signalFrame.values[underExcitationAction] = 0.0;
    for (auto model : {OEL_LOC, UEL_LOC}) {
        auto* limiter = (model == OEL_LOC) ? oel : uel;
        if (limiter != nullptr && limiter->isEnabled()) {
            auto limiterInputs = signalRoutes[model].values(
                {.hostInputs = signalFrame.values,
                 .hostInputLocs = nullptr,
                 .stateData = emptyStateData,
                 .solverMode = cLocalSolverMode});
            IOdata limiterFieldSet;
            limiter->dynInitializeB(limiterInputs, {}, limiterFieldSet);
            signalFrame.values[(model == OEL_LOC) ? overExcitationAction : underExcitationAction] =
                limiter->getOutput();
        }
    }
    if ((voltageCompensator != nullptr) && (voltageCompensator->isEnabled())) {
        auto compensatorInputs = signalRoutes[VOLTAGE_COMPENSATOR_LOC].values(
            {.hostInputs = signalFrame.values,
             .hostInputLocs = nullptr,
             .stateData = emptyStateData,
             .solverMode = cLocalSolverMode});
        IOdata compensatorFieldSet;
        voltageCompensator->dynInitializeB(compensatorInputs, {}, compensatorFieldSet);
        signalFrame.values[exciterVoltage] = voltageCompensator->getOutput();
    }
    if ((ext != nullptr) && (ext->isEnabled())) {
        auto exciterInputs = signalRoutes[EXCITER_LOC].values(
            {.hostInputs = signalFrame.values,
             .hostInputLocs = nullptr,
             .stateData = emptyStateData,
             .solverMode = cLocalSolverMode});

        localDesiredOutput[0] = m_Eft;
        ext->dynInitializeB(exciterInputs, localDesiredOutput, computedFieldSet);

        //    ext->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
        // Vset=inputSetup[1];
    }
    if ((gov != nullptr) && (gov->isEnabled())) {
        auto governorInputs = signalRoutes[GOVERNOR_LOC].values(
            {.hostInputs = signalFrame.values,
             .hostInputLocs = nullptr,
             .stateData = emptyStateData,
             .solverMode = cLocalSolverMode});

        localDesiredOutput[0] = Pset * scale;
        if (isoc != nullptr) {
            localDesiredOutput[0] += isoc->getOutput() * scale;
        }
        gov->dynInitializeB(governorInputs, localDesiredOutput, computedFieldSet);

        //     gov->guessState (prevTime, m_state.data (), m_dstate_dt.data (), cLocalbSolverMode);
    }

    if ((pss != nullptr) && (pss->isEnabled())) {
        // The PSS initialization contract uses mechanical power as its
        // initial electrical-power input and raw terminal voltage.
        const auto previousVoltage = signalFrame.values[exciterVoltage];
        const auto torqueIndex =
            machineSignalBase + static_cast<index_t>(MachineControllerSignal::ELECTRICAL_TORQUE);
        const auto previousTorque = signalFrame.values[torqueIndex];
        signalFrame.values[exciterVoltage] = voltage;
        signalFrame.values[torqueIndex] = m_Pmech;
        auto pssInputs = signalRoutes[PSS_LOC].values(
            {.hostInputs = signalFrame.values,
             .hostInputLocs = nullptr,
             .stateData = emptyStateData,
             .solverMode = cLocalSolverMode});
        signalFrame.values[exciterVoltage] = previousVoltage;
        signalFrame.values[torqueIndex] = previousTorque;
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
        if ((sub->locIndex <= PSS_LOC) || (sub == voltageCompensator) || (sub == oel) ||
            (sub == uel)) {
            continue;
        }
        if (sub->isEnabled()) {
            if (sub == pSetControl && dynamic_cast<Scheduler*>(sub) != nullptr) {
                localDesiredOutput = {Pset};
            }
            sub->dynInitializeB(modelInputs, localDesiredOutput, computedFieldSet);
            localDesiredOutput.clear();
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
    if (!isEnabled()) {
        return;
    }
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
    if (signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator structure requires a full dynamic reset");
    }
    Generator::add(obj);
}

void DynamicGenerator::add(GridSubModel* obj)
{
    if (signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator structure requires a full dynamic reset");
    }
    if (dynamic_cast<VoltageCompensator*>(obj) != nullptr) {
        voltageCompensator = static_cast<VoltageCompensator*>(
            replaceModel(obj, voltageCompensator, VOLTAGE_COMPENSATOR_LOC));
    } else if (auto* limiter = dynamic_cast<ExcitationLimiter*>(obj)) {
        if (limiter->role() == ExcitationLimiter::Role::OVER) {
            oel = static_cast<ExcitationLimiter*>(replaceModel(obj, oel, OEL_LOC));
        } else {
            uel = static_cast<ExcitationLimiter*>(replaceModel(obj, uel, UEL_LOC));
        }
    } else if (dynamic_cast<Exciter*>(obj) != nullptr) {
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

void DynamicGenerator::remove(CoreObject* obj)
{
    if (signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator structure requires a full dynamic reset");
    }
    if (obj == pss) {
        pss = nullptr;
    } else if (obj == ext) {
        ext = nullptr;
    } else if (obj == gov) {
        gov = nullptr;
    } else if (obj == genModel) {
        genModel = nullptr;
    } else if (obj == voltageCompensator) {
        voltageCompensator = nullptr;
    } else if (obj == oel) {
        oel = nullptr;
    } else if (obj == uel) {
        uel = nullptr;
    } else if (obj == pSetControl) {
        pSetControl = nullptr;
    } else if (obj == vSetControl) {
        vSetControl = nullptr;
    } else if (obj == isoc) {
        isoc = nullptr;
    }
    if (obj == mechanicalPowerSource) {
        mechanicalPowerSource = nullptr;
    }
    if (obj == sched) {
        sched = nullptr;
    }
    GridComponent::remove(obj);
}

void DynamicGenerator::setMechanicalPowerSource(GridSubModel* source, index_t outputIndex)
{
    if (signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator structure requires a full dynamic reset");
    }
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
    if (signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator structure requires a full dynamic reset");
    }
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
    if (signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator structure requires a full dynamic reset");
    }
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
        if (!signalRoutesReady) {
            throw InvalidParameterValue("dynamic generator signal routes require initialization");
        }
        const double scale = machineBasePower / systemBasePower;
        signalFrame.values[terminalVoltage] = inputs[VOLTAGE_IN_LOCATION];
        signalFrame.values[terminalAngle] = inputs[ANGLE_IN_LOCATION];
        signalFrame.values[controllerOmega] = genModel->getFreq(emptyStateData, cLocalSolverMode);
        signalFrame.values[isochronousFrequency] = signalFrame.values[controllerOmega] - 1.0;
        signalFrame.values[activePowerCommand] = Pset / scale;
        signalFrame.values[mechanicalPower] = m_Pmech;
        signalFrame.values[fieldVoltage] = m_Eft;
        signalFrame.values[voltageSetpoint] = 1.0;
        signalFrame.values[exciterVoltage] = signalFrame.values[terminalVoltage];
        signalFrame.values[stabilizerOutput] = 0.0;
        writeModelInputs(GEN_MODEL_LOC, emptyStateData, sMode);

        if ((gov != nullptr) && gov->isEnabled()) {
            const auto governorSignals =
                genModel->getMachineControllerSignals(subInputs.inputs[GEN_MODEL_LOC],
                                                      emptyStateData,
                                                      cLocalSolverMode);
            signalFrame.values[governorElectricalPower] =
                governorSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)];
            writeModelInputs(GOVERNOR_LOC, emptyStateData, sMode);
            gov->timestep(time, subInputs.inputs[GOVERNOR_LOC], sMode);
        }
        auto* pmechSource = boundMechanicalPowerSource;
        if ((pmechSource != nullptr) && pmechSource->isEnabled()) {
            m_Pmech = pmechSource->getOutput(getMechanicalPowerOutput());
        }
        signalFrame.values[mechanicalPower] = m_Pmech;
        writeModelInputs(GEN_MODEL_LOC, emptyStateData, sMode);

        const auto machineSignals =
            genModel->getMachineControllerSignals(subInputs.inputs[GEN_MODEL_LOC],
                                                  emptyStateData,
                                                  cLocalSolverMode);
        for (index_t signalIndex = 0; signalIndex < machineControllerSignalCount; ++signalIndex) {
            signalFrame.values[machineSignalBase + signalIndex] = machineSignals[signalIndex];
        }
        signalFrame.values[governorElectricalPower] =
            machineSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)];

        signalFrame.values[overExcitationAction] = 0.0;
        signalFrame.values[underExcitationAction] = 0.0;
        for (auto model : {OEL_LOC, UEL_LOC}) {
            auto* limiter = (model == OEL_LOC) ? oel : uel;
            if (limiter != nullptr && limiter->isEnabled()) {
                writeModelInputs(model, emptyStateData, sMode);
                limiter->timestep(time, subInputs.inputs[model], sMode);
                signalFrame
                    .values[(model == OEL_LOC) ? overExcitationAction : underExcitationAction] =
                    limiter->getOutput();
            }
        }

        if ((pss != nullptr) && pss->isEnabled()) {
            writeModelInputs(PSS_LOC, emptyStateData, sMode);
            pss->timestep(time, subInputs.inputs[PSS_LOC], sMode);
        }
        if ((boundStabilizer != nullptr) && boundStabilizer->isEnabled()) {
            signalFrame.values[stabilizerOutput] = boundStabilizer->getOutput();
        }

        if ((ext != nullptr) && ext->isEnabled()) {
            if ((voltageCompensator != nullptr) && voltageCompensator->isEnabled()) {
                writeModelInputs(VOLTAGE_COMPENSATOR_LOC, emptyStateData, sMode);
                signalFrame.values[exciterVoltage] =
                    voltageCompensator->getOutput(subInputs.inputs[VOLTAGE_COMPENSATOR_LOC],
                                                  emptyStateData,
                                                  cLocalSolverMode,
                                                  0);
            }
            writeModelInputs(EXCITER_LOC, emptyStateData, sMode);
            ext->timestep(time, subInputs.inputs[EXCITER_LOC], sMode);
            m_Eft = ext->getOutput();
        }
        signalFrame.values[fieldVoltage] = m_Eft;
        writeModelInputs(GEN_MODEL_LOC, emptyStateData, sMode);
        genModel->timestep(time, subInputs.inputs[GEN_MODEL_LOC], sMode);
        auto vals =
            genModel->getOutputs(subInputs.inputs[GEN_MODEL_LOC], emptyStateData, cLocalSolverMode);
        P = vals[POUT_LOCATION] * scale;
        Q = vals[QOUT_LOCATION] * scale;
    }
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
        if (signalRoutesReady) {
            throw InvalidParameterValue(
                "dynamic generator structure requires a full dynamic reset");
        }
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
    const auto& routedLocations = routeInputLocations[GEN_MODEL_LOC];
    if (routedLocations.needsTranslation()) {
        MatrixDataCustomWriteOnly<double> translatedMatrix;
        translatedMatrix.setFunction(
            [&scaledMatrixData, &routedLocations](index_t row, index_t column, double value) {
                routedLocations.assign(scaledMatrixData, row, column, value);
            });
        genModel->ioPartialDerivatives(subInputs.inputs[GEN_MODEL_LOC],
                                       stateDataValue,
                                       translatedMatrix,
                                       subInputLocs.genModelInputLocsInternal,
                                       sMode);
    } else {
        genModel->ioPartialDerivatives(subInputs.inputs[GEN_MODEL_LOC],
                                       stateDataValue,
                                       scaledMatrixData,
                                       subInputLocs.genModelInputLocsInternal,
                                       sMode);
    }
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
        updateLocalCache(inputs, stateDataValue, sMode);
        generateSubModelInputLocs(inputLocs, stateDataValue, sMode, false);
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

    // compute the Jacobian
    for (auto* sub : getSubObjects()) {
        if (sub->isEnabled()) {
            const auto& routedLocations = routeInputLocations[sub->locIndex];
            if (routedLocations.needsTranslation()) {
                MatrixDataCustomWriteOnly<double> translatedMatrix;
                translatedMatrix.setFunction([&matrixDataValue, &routedLocations](index_t row,
                                                                                  index_t column,
                                                                                  double value) {
                    routedLocations.assign(matrixDataValue, row, column, value);
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
    if ((object == "voltagecompensator") || (object == "vcomp") || (object == "ieeevc")) {
        return voltageCompensator;
    }
    if ((object == "oel") || (object == "overexcitationlimiter")) {
        return oel;
    }
    if ((object == "uel") || (object == "underexcitationlimiter")) {
        return uel;
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

DynamicGenerator::SubModelInputs::SubModelInputs(): inputs(subModelSlotCount)
{
    inputs[GEN_MODEL_LOC].resize(4);
    inputs[EXCITER_LOC].resize(exciterInputCount);
    inputs[GOVERNOR_LOC].resize(3);
    inputs[PSS_LOC].resize(pssInputCount);
    inputs[VOLTAGE_COMPENSATOR_LOC].resize(voltageCompensatorInputCount);
    inputs[OEL_LOC].resize(excitationLimiterInputCount);
    inputs[UEL_LOC].resize(excitationLimiterInputCount);
}

DynamicGenerator::SubModelInputLocs::SubModelInputLocs():
    genModelInputLocsInternal(4), genModelInputLocsExternal(4), inputLocs(subModelSlotCount)
{
    inputLocs[GEN_MODEL_LOC].resize(4);
    inputLocs[EXCITER_LOC].resize(exciterInputCount);
    inputLocs[GOVERNOR_LOC].resize(3);
    inputLocs[PSS_LOC].resize(pssInputCount);
    inputLocs[VOLTAGE_COMPENSATOR_LOC].resize(voltageCompensatorInputCount);
    inputLocs[OEL_LOC].resize(excitationLimiterInputCount);
    inputLocs[UEL_LOC].resize(excitationLimiterInputCount);

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
    if (!signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator signal routes require initialization");
    }
    double frequency = subInputs.inputs[GOVERNOR_LOC][govOmegaInLocation];
    if (inputs.empty()) {
        auto out = bus->getOutputs(noInputs, stateDataValue, sMode);
        signalFrame.values[terminalVoltage] = out[VOLTAGE_IN_LOCATION];
        signalFrame.values[terminalAngle] = out[ANGLE_IN_LOCATION];
        frequency = out[FREQUENCY_IN_LOCATION];
    } else {
        signalFrame.values[terminalVoltage] = inputs[VOLTAGE_IN_LOCATION];
        signalFrame.values[terminalAngle] = inputs[ANGLE_IN_LOCATION];
        if (inputs.size() > FREQUENCY_IN_LOCATION) {
            frequency = inputs[FREQUENCY_IN_LOCATION];
        }
    }
    if (!opFlags[USES_BUS_FREQUENCY]) {
        frequency = genModel->getFreq(stateDataValue, sMode);
    }
    signalFrame.values[controllerOmega] = frequency;
    signalFrame.values[isochronousFrequency] = frequency - 1.0;
    if (isoc != nullptr) {
        writeModelInputs(ISOC_CONTROL_LOC, stateDataValue, sMode);
    }

    const double scale = systemBasePower / machineBasePower;
    activePowerCommandUnclamped = pSetControlUpdate(inputs, stateDataValue, sMode);
    const double pcontrol = gmlc::utilities::valLimit(activePowerCommandUnclamped, Pmin, Pmax);
    signalFrame.values[activePowerCommand] = pcontrol * scale;
    // The governor's mechanical output is evaluated before the new machine
    // electrical-power snapshot, matching the existing feedback order.
    signalFrame.values[governorElectricalPower] =
        subInputs.inputs[GOVERNOR_LOC][govElectricalPowerInLocation];
    writeModelInputs(GOVERNOR_LOC, stateDataValue, sMode);

    double pmech = pcontrol * scale;
    auto* pmechSource = boundMechanicalPowerSource;
    if ((pmechSource != nullptr) && (pmechSource->isEnabled())) {
        const auto& sourceInputs = (pmechSource == gov) ? subInputs.inputs[GOVERNOR_LOC] : noInputs;
        const SolverMode* outputMode = &sMode;
        StateData outputStateData = stateDataValue;
        if (isDifferentialOnly(sMode) && (sMode.pairedOffsetIndex != kNullLocation) &&
            (stateDataValue.algState != nullptr)) {
            const auto& pairedMode = offsets.getSolverMode(sMode.pairedOffsetIndex);
            if (pairedMode.algebraic &&
                (pmechSource->algSize(pairedMode) > getMechanicalPowerOutput())) {
                // Partitioned differential evaluations carry the algebraic
                // solution in StateData::algState.  Use the paired mode for
                // an algebraic controller output; otherwise GridComponent's
                // output fallback returns the first differential state.
                outputMode = &pairedMode;
                outputStateData.state = stateDataValue.algState;
            }
        }
        pmech = pmechSource->getOutput(sourceInputs,
                                       outputStateData,
                                       *outputMode,
                                       getMechanicalPowerOutput());
    }
    mechanicalPowerWasInvalid = std::abs(pmech) > 1e25;
    if (mechanicalPowerWasInvalid) {
        pmech = 0.0;
    }
    signalFrame.values[mechanicalPower] = pmech;
    signalFrame.values[voltageSetpoint] = vSetControlUpdate(inputs, stateDataValue, sMode);
    signalFrame.values[fieldVoltage] = subInputs.inputs[GEN_MODEL_LOC][genModelEftInLocation];
    writeModelInputs(GEN_MODEL_LOC, stateDataValue, sMode);
    const auto machineSignals =
        genModel->getMachineControllerSignals(subInputs.inputs[GEN_MODEL_LOC],
                                              stateDataValue,
                                              sMode);
    for (index_t signalIndex = 0; signalIndex < machineControllerSignalCount; ++signalIndex) {
        signalFrame.values[machineSignalBase + signalIndex] = machineSignals[signalIndex];
    }
    signalFrame.values[governorElectricalPower] =
        machineSignals[static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)];
    signalFrame.values[overExcitationAction] = 0.0;
    signalFrame.values[underExcitationAction] = 0.0;
    for (auto model : {OEL_LOC, UEL_LOC}) {
        auto* limiter = (model == OEL_LOC) ? oel : uel;
        if (limiter != nullptr && limiter->isEnabled()) {
            writeModelInputs(model, stateDataValue, sMode);
            const SolverMode* outputMode = &sMode;
            StateData outputStateData = stateDataValue;
            if (isDifferentialOnly(sMode) && sMode.pairedOffsetIndex != kNullLocation &&
                stateDataValue.algState != nullptr) {
                const auto& pairedMode = offsets.getSolverMode(sMode.pairedOffsetIndex);
                if (pairedMode.algebraic && limiter->algSize(pairedMode) > 0) {
                    outputMode = &pairedMode;
                    outputStateData.state = stateDataValue.algState;
                }
            }
            signalFrame.values[(model == OEL_LOC) ? overExcitationAction : underExcitationAction] =
                limiter->getOutput(subInputs.inputs[model], outputStateData, *outputMode);
        }
    }
    writeModelInputs(GOVERNOR_LOC, stateDataValue, sMode);
    signalFrame.values[exciterVoltage] = signalFrame.values[terminalVoltage];
    if ((voltageCompensator != nullptr) && voltageCompensator->isEnabled()) {
        writeModelInputs(VOLTAGE_COMPENSATOR_LOC, stateDataValue, sMode);
        signalFrame.values[exciterVoltage] =
            voltageCompensator->getOutput(subInputs.inputs[VOLTAGE_COMPENSATOR_LOC],
                                          stateDataValue,
                                          sMode,
                                          0);
    }
    if ((boundStabilizer != nullptr) && boundStabilizer->isEnabled()) {
        writeModelInputs(PSS_LOC, stateDataValue, sMode);
        signalFrame.values[stabilizerOutput] =
            boundStabilizer->getOutput(subInputs.inputs[PSS_LOC], stateDataValue, sMode);
    } else {
        signalFrame.values[stabilizerOutput] = 0.0;
    }
    writeModelInputs(EXCITER_LOC, stateDataValue, sMode);
    double eft = m_Eft;
    if ((ext != nullptr) && (ext->isEnabled())) {
        const SolverMode* outputMode = &sMode;
        StateData outputStateData = stateDataValue;
        if (isDifferentialOnly(sMode) && (sMode.pairedOffsetIndex != kNullLocation) &&
            (stateDataValue.algState != nullptr)) {
            const auto& pairedMode = offsets.getSolverMode(sMode.pairedOffsetIndex);
            if (pairedMode.algebraic && (ext->algSize(pairedMode) > 0)) {
                // The exciter field output is an algebraic state.  A
                // differential-only callback still carries that state in
                // StateData::algState, but GridComponent::getOutput uses the
                // differential mode's first state when called with the
                // differential mode.  Evaluate the output in the paired
                // algebraic mode so the generator sees Efd rather than the
                // exciter's first dynamic state.
                outputMode = &pairedMode;
                outputStateData.state = stateDataValue.algState;
            }
        }
        eft = ext->getOutput(subInputs.inputs[EXCITER_LOC], outputStateData, *outputMode, 0);
    }
    signalFrame.values[fieldVoltage] = eft;
    writeModelInputs(GEN_MODEL_LOC, stateDataValue, sMode);

    if (!stateDataValue.empty()) {
        subInputs.seqID = stateDataValue.seqID;
    }
}

void DynamicGenerator::generateSubModelInputLocs(const IOlocs& inputLocs,
                                                 const StateData& stateDataValue,
                                                 const SolverMode& sMode,
                                                 bool includeControllerLocations)
{
    // Source identity is fixed. Locations and sparse derivatives belong to
    // this solver mode and are rebuilt for every Jacobian assembly.
    if (!signalRoutesReady) {
        throw InvalidParameterValue("dynamic generator signal routes require initialization");
    }
    auto& sourceLocations = signalFrame.locations;
    std::fill(sourceLocations.begin(), sourceLocations.end(), kNullLocation);
    for (auto& terms : signalFrame.derivatives) {
        terms.clear();
    }
    sourceLocations[terminalVoltage] = inputLocs[VOLTAGE_IN_LOCATION];
    sourceLocations[terminalAngle] = inputLocs[ANGLE_IN_LOCATION];

    if (genModel->checkFlag(USES_BUS_FREQUENCY)) {
        sourceLocations[controllerOmega] = inputLocs[FREQUENCY_IN_LOCATION];
    } else {
        index_t location = kNullLocation;
        genModel->getFreq(stateDataValue, sMode, &location);
        sourceLocations[controllerOmega] = location;
    }
    sourceLocations[isochronousFrequency] = sourceLocations[controllerOmega];
    sourceLocations[voltageSetpoint] = vSetLocation(sMode);

    const double scale = systemBasePower / machineBasePower;
    if (activePowerCommandUnclamped >= Pmin && activePowerCommandUnclamped <= Pmax) {
        const auto sourceLocation = pSetLocation(sMode);
        if (sourceLocation != kNullLocation) {
            signalFrame.derivatives[activePowerCommand].push_back(
                {.location = sourceLocation, .value = scale});
        }
        if (opFlags[ISOCHRONOUS_OPERATION] && isoc != nullptr) {
            signalFrame.derivatives[activePowerCommand].push_back(
                {.location = isoc->getOutputLoc(sMode, 0), .value = 1.0});
        }
    }

    auto* pmechSource = boundMechanicalPowerSource;
    if (!mechanicalPowerWasInvalid) {
        if ((pmechSource != nullptr) && pmechSource->isEnabled()) {
            index_t location = pmechSource->getOutputLoc(sMode, getMechanicalPowerOutput());
            if (isDifferentialOnly(sMode) && (sMode.pairedOffsetIndex != kNullLocation)) {
                const auto& pairedMode = offsets.getSolverMode(sMode.pairedOffsetIndex);
                if (pairedMode.algebraic && (pmechSource->algSize(pairedMode) > 0)) {
                    location = kNullLocation;
                }
            }
            sourceLocations[mechanicalPower] = location;
        } else {
            signalFrame.derivatives[mechanicalPower] = signalFrame.derivatives[activePowerCommand];
        }
    }

    if ((ext != nullptr) && ext->isEnabled()) {
        index_t location = ext->getOutputLoc(sMode, 0);
        if (isDifferentialOnly(sMode) && (sMode.pairedOffsetIndex != kNullLocation)) {
            const auto& pairedMode = offsets.getSolverMode(sMode.pairedOffsetIndex);
            if (pairedMode.algebraic && (ext->algSize(pairedMode) > 0)) {
                location = kNullLocation;
            }
        }
        sourceLocations[fieldVoltage] = location;
    }

    sourceLocations[exciterVoltage] =
        ((voltageCompensator != nullptr) && voltageCompensator->isEnabled()) ?
        voltageCompensator->getOutputLoc(sMode, 0) :
        sourceLocations[terminalVoltage];
    if ((boundStabilizer != nullptr) && boundStabilizer->isEnabled()) {
        sourceLocations[stabilizerOutput] = boundStabilizer->getOutputLoc(sMode, 0);
    }
    for (auto model : {OEL_LOC, UEL_LOC}) {
        auto* limiter = (model == OEL_LOC) ? oel : uel;
        if (limiter != nullptr && limiter->isEnabled()) {
            index_t location = limiter->getOutputLoc(sMode, 0);
            if (isDifferentialOnly(sMode) && sMode.pairedOffsetIndex != kNullLocation) {
                const auto& pairedMode = offsets.getSolverMode(sMode.pairedOffsetIndex);
                if (pairedMode.algebraic && limiter->algSize(pairedMode) > 0) {
                    location = kNullLocation;
                }
            }
            sourceLocations[(model == OEL_LOC) ? overExcitationAction : underExcitationAction] =
                location;
        }
    }

    const ControlSignalContext context{.hostInputs = signalFrame.values,
                                       .hostInputLocs = &sourceLocations,
                                       .stateData = stateDataValue,
                                       .solverMode = sMode,
                                       .hostInputDerivatives = &signalFrame.derivatives};
    signalRoutes[GEN_MODEL_LOC].writeInputLocations(context, routeInputLocations[GEN_MODEL_LOC]);
    subInputLocs.inputLocs[GEN_MODEL_LOC] = routeInputLocations[GEN_MODEL_LOC].locations;
    subInputLocs.genModelInputLocsInternal = subInputLocs.inputLocs[GEN_MODEL_LOC];
    subInputLocs.genModelInputLocsInternal[VOLTAGE_IN_LOCATION] = kNullLocation;
    subInputLocs.genModelInputLocsInternal[ANGLE_IN_LOCATION] = kNullLocation;
    subInputLocs.genModelInputLocsExternal = subInputLocs.inputLocs[GEN_MODEL_LOC];
    subInputLocs.genModelInputLocsExternal[genModelEftInLocation] = kNullLocation;
    subInputLocs.genModelInputLocsExternal[genModelPmechInLocation] = kNullLocation;

    if (!includeControllerLocations) {
        subInputLocs.seqID = stateDataValue.seqID;
        return;
    }

    const bool machineSignalsUsed =
        ((ext != nullptr) && ext->isEnabled() && (ext->numInputs() > exciterMachineSignalBase)) ||
        ((gov != nullptr) && gov->isEnabled() &&
         (gov->numInputs() > govElectricalPowerInLocation)) ||
        ((pss != nullptr) && pss->isEnabled()) ||
        ((voltageCompensator != nullptr) && voltageCompensator->isEnabled()) ||
        ((oel != nullptr) && oel->isEnabled()) || ((uel != nullptr) && uel->isEnabled());
    if (machineSignalsUsed) {
        const auto machineDerivatives =
            genModel->getMachineControllerSignalDerivatives(subInputs.inputs[GEN_MODEL_LOC],
                                                            stateDataValue,
                                                            subInputLocs.inputLocs[GEN_MODEL_LOC],
                                                            sMode);
        for (index_t signalIndex = 0; signalIndex < machineControllerSignalCount; ++signalIndex) {
            auto& terms = signalFrame.derivatives[machineSignalBase + signalIndex];
            for (const auto& derivative : machineDerivatives[signalIndex]) {
                routeInputLocations[GEN_MODEL_LOC].appendExpanded(derivative.location,
                                                                  derivative.value,
                                                                  terms);
            }
        }
    }
    if (!isDifferentialOnly(sMode)) {
        signalFrame.derivatives[governorElectricalPower] =
            signalFrame
                .derivatives[machineSignalBase +
                             static_cast<index_t>(MachineControllerSignal::ELECTRICAL_POWER)];
    }

    for (auto model : {EXCITER_LOC,
                       GOVERNOR_LOC,
                       PSS_LOC,
                       VOLTAGE_COMPENSATOR_LOC,
                       ISOC_CONTROL_LOC,
                       OEL_LOC,
                       UEL_LOC}) {
        if ((model == EXCITER_LOC && (ext == nullptr || !ext->isEnabled())) ||
            (model == GOVERNOR_LOC && (gov == nullptr || !gov->isEnabled())) ||
            (model == PSS_LOC && (pss == nullptr || !pss->isEnabled())) ||
            (model == VOLTAGE_COMPENSATOR_LOC &&
             (voltageCompensator == nullptr || !voltageCompensator->isEnabled())) ||
            (model == ISOC_CONTROL_LOC && isoc == nullptr) ||
            (model == OEL_LOC && (oel == nullptr || !oel->isEnabled())) ||
            (model == UEL_LOC && (uel == nullptr || !uel->isEnabled()))) {
            continue;
        }
        signalRoutes[model].writeInputLocations(context, routeInputLocations[model]);
        subInputLocs.inputLocs[model] = routeInputLocations[model].locations;
    }
    subInputLocs.seqID = stateDataValue.seqID;
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
