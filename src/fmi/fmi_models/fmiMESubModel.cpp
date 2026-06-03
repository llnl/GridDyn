/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiMESubModel.h"

#include "../fmi_import/fmiLibraryManager.h"
#include "../fmi_import/fmiObjects.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "outputEstimator.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::fmi {
using gmlc::utilities::vectorMultAdd;

FmiMESubModel::FmiMESubModel(const std::string& newName,
                             std::shared_ptr<Fmi2ModelExchangeObject> fmi):
    GridSubModel(newName), me(std::move(fmi))
{
}

FmiMESubModel::FmiMESubModel(std::shared_ptr<Fmi2ModelExchangeObject> fmi): me(std::move(fmi)) {}

FmiMESubModel::~FmiMESubModel() = default;

CoreObject* FmiMESubModel::clone(CoreObject* obj) const
{
    auto* gco = cloneBase<FmiMESubModel, GridSubModel>(this, obj);
    if (gco == nullptr) {
        return obj;
    }
    return gco;
}

bool FmiMESubModel::isLoaded() const
{
    return static_cast<bool>(me);
}

void FmiMESubModel::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    // printf("GridDyn Pflow A\n");
    if (CHECK_CONTROLFLAG(flags, force_constant_pflow_initialization)) {
        //    printf("GridDyn pflow_init_required\n");
        opFlags.set(pflow_init_required);
        me->setMode(FmuMode::INITIALIZATION_MODE);
        //    printf("finished setting init mode\n");
    }
    prevTime = time0;
}
void FmiMESubModel::pFlowObjectInitializeB()
{
    if (opFlags[pflow_init_required]) {
        // printf("enter continuous time mode\n");
        me->setMode(FmuMode::CONTINUOUS_TIME_MODE);
        // printf("finished setting continuous time mode");
        oEst.resize(m_outputSize);
        probeFMU();
        opFlags.set(pFlow_initialized);
    }
}

void FmiMESubModel::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    prevTime = time0;
}

void FmiMESubModel::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& /*desiredOutput*/,
                                         IOdata& /*inputSet*/)
{
    if (opFlags[pflow_init_required]) {
        // printf("GridDyn Dyn B pflowINit required\n");
        if (opFlags[pFlow_initialized]) {
            me->getStates(m_state.data());
            me->setTime(prevTime - 0.01);

            if (opFlags[USE_OUTPUT_ESTIMATOR]) {
                // if we require the use of output estimators flag that to the simulation and
                // load the information for the estimator
                alert(this, SINGLE_STEP_REQUIRED);

                loadOutputJac();
                for (index_t pp = 0; pp < m_outputSize; ++pp) {
                    if (outputInformation[pp].refMode >= RefMode::LEVEL4) {
                        const double val = me->getOutput(pp);
                        oEst[pp]->update(prevTime, val, inputs, m_state.data());
                    }
                }
            }
            opFlags.set(dyn_initialized);
        }
    } else {
        // printf("GridDyn Dyn B pflowInit NOT required\n");
        me->setMode(FmuMode::INITIALIZATION_MODE);
        if (!inputs.empty()) {
            me->setInputs(inputs.data());
        }
        me->setMode(FmuMode::CONTINUOUS_TIME_MODE);
        if (!m_state.empty()) {
            me->getStates(m_state.data());
        }

        oEst.resize(m_outputSize);
        probeFMU();  // probe the fmu
        if (opFlags[USE_OUTPUT_ESTIMATOR]) {
            // if we require the use of output estimators flag that to the simulation and load
            // the information for the estimator
            alert(this, SINGLE_STEP_REQUIRED);
            loadOutputJac();
        }
        me->setTime(prevTime - 0.01);
    }
}

void FmiMESubModel::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    int strpcnt{0};
    const auto* info = me->getFmiInformation();
    auto vcnt = info->getCounts("variables");
    switch (pstype) {
        case ParamStringType::ALL:
            pstr.reserve(pstr.size() + info->getCounts("params") + info->getCounts("inputs") -
                         m_inputSize);

            for (int kk = 0; kk < vcnt; ++kk) {
                if (info->getVariableInformation(kk).type == FmiVariableType::STRING) {
                    ++strpcnt;
                } else if (checkType(info->getVariableInformation(kk),
                                     FmiVariableType::NUMERIC,
                                     FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }

            GridSubModel::getParameterStrings(pstr, ParamStringType::NUMERIC);
            pstr.reserve(pstr.size() + strpcnt + 1);
            pstr.emplace_back("#");
            for (int kk = 0; kk < vcnt; ++kk) {
                if (checkType(info->getVariableInformation(kk),
                              FmiVariableType::STRING,
                              FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }
            GridSubModel::getParameterStrings(pstr, ParamStringType::STR);
            break;
        case ParamStringType::LOCAL_NUM:
            pstr.reserve(info->getCounts("params") + info->getCounts("inputs") - m_inputSize);
            pstr.resize(0);
            for (int kk = 0; kk < vcnt; ++kk) {
                if (checkType(info->getVariableInformation(kk),
                              FmiVariableType::NUMERIC,
                              FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }
            break;
        case ParamStringType::LOCAL_STR:
            pstr.reserve(info->getCounts("params") + info->getCounts("inputs") - m_inputSize);
            pstr.resize(0);
            for (int kk = 0; kk < vcnt; ++kk) {
                if (checkType(info->getVariableInformation(kk),
                              FmiVariableType::STRING,
                              FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }
            break;
        case ParamStringType::LOCAL_FLAGS:
            pstr.reserve(info->getCounts("params") + info->getCounts("inputs") - m_inputSize);
            pstr.resize(0);
            for (int kk = 0; kk < vcnt; ++kk) {
                if (checkType(info->getVariableInformation(kk),
                              FmiVariableType::BOOLEAN,
                              FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }
            break;
        case ParamStringType::NUMERIC:
            pstr.reserve(pstr.size() + info->getCounts("params") + info->getCounts("inputs") -
                         m_inputSize);
            for (int kk = 0; kk < vcnt; ++kk) {
                if (checkType(info->getVariableInformation(kk),
                              FmiVariableType::NUMERIC,
                              FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }
            GridSubModel::getParameterStrings(pstr, ParamStringType::NUMERIC);
            break;
        case ParamStringType::STR:
            pstr.reserve(pstr.size() + info->getCounts("params") + info->getCounts("inputs") -
                         m_inputSize);
            for (int kk = 0; kk < vcnt; ++kk) {
                if (checkType(info->getVariableInformation(kk),
                              FmiVariableType::STRING,
                              FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }
            GridSubModel::getParameterStrings(pstr, ParamStringType::STR);
            break;
        case ParamStringType::FLAGS:
            pstr.reserve(pstr.size() + info->getCounts("params") + info->getCounts("inputs") -
                         m_inputSize);
            for (int kk = 0; kk < vcnt; ++kk) {
                if (checkType(info->getVariableInformation(kk),
                              FmiVariableType::BOOLEAN,
                              FmiCausalityType::PARAMETER)) {
                    pstr.push_back(info->getVariableInformation(kk).name);
                }
            }
            GridSubModel::getParameterStrings(pstr, ParamStringType::FLAGS);
            break;
    }
}

stringVec FmiMESubModel::getOutputNames() const
{
    return me->getOutputNames();
}

stringVec FmiMESubModel::getInputNames() const
{
    return me->getInputNames();
}

void FmiMESubModel::set(std::string_view param, std::string_view val)
{
    using gmlc::utilities::stringOps::splitline;
    using gmlc::utilities::stringOps::trim;

    if ((param == "fmu") || (param == "file")) {
        if (!(me)) {
            try {
                // printf("loading instance of fmi %s\n", val.c_str());
                me = FmiLibraryManager::instance().createModelExchangeInstance(std::string{val},
                                                                               getName());
            }
            catch (const std::filesystem::filesystem_error& fse) {
                // printf("error loading object %s\n",fse.what());
                logging::error(this, "file system error{}", fse.what());
            }
            if (!me) {
                // printf("unable to load ME object\n");
                logging::error(this, "Unable to load FMU {}", getName());
            }
            if (!paramBuffer.empty()) {
                paramBuffer.apply(me);
            }
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if (param == "outputs") {
        auto ssep = splitline(val);
        trim(ssep);
        me->setOutputVariables(ssep);
        m_outputSize = me->outputSize();
    } else if (param == "inputs") {
        auto ssep = splitline(val);
        trim(ssep);
        me->setInputVariables(ssep);
        m_inputSize = me->inputSize();
        // updateDependencyInfo();
    } else {
        if (me) {
            const bool isparam = me->isParameter(std::string{param}, FmiVariableType::STRING);
            if (isparam) {
                makeSettableState();
                me->set(std::string{param}, std::string{val});
                resetState();
            } else {
                GridSubModel::set(param, val);
            }
        } else {
            try {
                GridSubModel::set(param, val);
            }
            catch (const UnrecognizedParameter&) {
                paramBuffer.set(std::string{param}, std::string{val});
            }
        }
    }
}

void FmiMESubModel::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "timestep") || (param == "localintegrationtime")) {
        localIntegrationTime = val;
    } else {
        if (me) {
            const bool isparam = me->isParameter(std::string{param}, FmiVariableType::NUMERIC);
            if (isparam) {
                makeSettableState();
                me->set(std::string{param}, val);
                resetState();
            } else {
                GridSubModel::set(param, val, unitType);
            }
        } else {
            try {
                GridSubModel::set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                paramBuffer.set(std::string{param}, val, unitType);
            }
        }
    }
}

double FmiMESubModel::get(std::string_view param, units::unit unitType) const
{
    if (param == "localintegrationtime") {
        return static_cast<double>(localIntegrationTime);
    }
    if ((me) && (me->isVariable(std::string{param}, FmiVariableType::NUMERIC))) {
        return me->get<double>(std::string{param});
    }
    return GridSubModel::get(param, unitType);
}

StateSizes FmiMESubModel::localStateSizes(const SolverMode& sMode) const
{
    StateSizes stateSizeInfo;
    if (hasDifferential(sMode)) {
        stateSizeInfo.diffSize = m_stateSize;
    } else if (!isDynamic(sMode) && opFlags[pflow_init_required]) {
        stateSizeInfo.algSize = m_stateSize;
    }
    return stateSizeInfo;
}

count_t FmiMESubModel::localJacobianCount(const SolverMode& sMode) const
{
    count_t jacSize = 0;
    if (hasDifferential(sMode) || (!isDynamic(sMode) && opFlags[pflow_init_required])) {
        jacSize = m_jacElements;
    }
    return jacSize;
}

std::pair<count_t, count_t> FmiMESubModel::LocalRootCount(const SolverMode& /* sMode */) const
{
    return {0, m_eventCount};
}

void FmiMESubModel::setState(CoreTime time,
                             const double state[],
                             const double dstateDt[],
                             const SolverMode& sMode)
{
    if (hasDifferential(sMode)) {
        auto loc = offsets.getDiffOffset(sMode);
        if (m_stateSize > 0) {
            me->setStates(state + loc);
            m_state.assign(state + loc, state + loc + m_stateSize);
            m_dstate_dt.assign(dstateDt + loc, dstateDt + loc + m_stateSize);
        }

        me->setTime(time);
        int eventMode;
        int terminate;
        me->completedIntegratorStep(fmi2True, &eventMode, &terminate);

        if ((opFlags[USE_OUTPUT_ESTIMATOR]) && (!opFlags[FIXED_OUTPUT_INTERVAL])) {
            IOdata inputValues(m_inputSize);

            me->getCurrentInputs(inputValues.data());
            for (index_t pp = 0; pp < m_outputSize; ++pp) {
                if (outputInformation[pp].refMode >= RefMode::LEVEL4) {
                    double val;
                    val = me->getOutput(pp);
                    const bool reload = oEst[pp]->update(time,
                                                         val,
                                                         inputValues,
                                                         state + offsets.getDiffOffset(sMode));
                    if (reload) {
                        loadOutputJac(static_cast<int>(pp));
                    }
                }
            }
        }
    } else if (!isDynamic(sMode) && (opFlags[pflow_init_required])) {
        auto loc = offsets.getAlgOffset(sMode);
        if (m_stateSize > 0) {
            me->setStates(state + loc);
            m_state.assign(state + loc, state + loc + m_stateSize);
        }
        me->setTime(time);
        int eventMode;
        int terminate;
        me->completedIntegratorStep(fmi2True, &eventMode, &terminate);
    }
    prevTime = time;
}
// for saving the state
void FmiMESubModel::guessState(CoreTime /*time*/,
                               double state[],
                               double dstateDt[],
                               const SolverMode& sMode)
{
    if (m_stateSize == 0) {
        return;
    }
    if (hasDifferential(sMode)) {
        auto loc = offsets.getDiffOffset(sMode);
        me->getStates(state + loc);
        me->getDerivatives(dstateDt + loc);
    } else if (!isDynamic(sMode) && (opFlags[pflow_init_required])) {
        auto loc = offsets.getAlgOffset(sMode);
        me->getStates(state + loc);
    }
}

void FmiMESubModel::getTols(double /*tols*/[], const SolverMode& /*sMode*/) {}

void FmiMESubModel::getStateName(stringVec& stNames,
                                 const SolverMode& sMode,
                                 const std::string& prefix) const
{
    if (hasDifferential(sMode)) {
        auto loc = offsets.getDiffOffset(sMode);
        if (static_cast<count_t>(stNames.size()) < loc + m_stateSize) {
            stNames.resize(loc + m_stateSize);
        }
        auto fmistNames = me->getStateNames();
        for (index_t kk = 0; kk < m_stateSize; ++kk) {
            if (prefix.empty()) {
                stNames[loc + kk] = getName() + ':' + fmistNames[kk];
            } else {
                stNames[loc + kk] = prefix + getName() + ':' + fmistNames[kk];
            }
        }
    } else if (!isDynamic(sMode) && (opFlags[pflow_init_required])) {
        auto loc = offsets.getAlgOffset(sMode);
        if (static_cast<count_t>(stNames.size()) < loc + m_stateSize) {
            stNames.resize(loc + m_stateSize);
        }
        auto fmistNames = me->getStateNames();
        for (index_t kk = 0; kk < m_stateSize; ++kk) {
            if (prefix.empty()) {
                stNames[loc + kk] = getName() + ':' + fmistNames[kk];
            } else {
                stNames[loc + kk] = prefix + '_' + getName() + ':' + fmistNames[kk];
            }
        }
    }
}

index_t FmiMESubModel::findIndex(std::string_view field, const SolverMode& /*sMode*/) const
{
    auto fmistNames = me->getStateNames();
    auto fnd = std::find(fmistNames.begin(), fmistNames.end(), field);
    if (fnd != fmistNames.end()) {
        return static_cast<index_t>(fnd - fmistNames.begin());
    }
    return kInvalidLocation;
}

void FmiMESubModel::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    if (hasDifferential(sMode)) {
        auto loc = offsets.getLocations(stateData, resid, sMode, this);
        derivative(inputs, stateData, resid, sMode);
        for (index_t ii = 0; ii < loc.diffSize; ++ii) {
            loc.destDiffLoc[ii] -= loc.dstateLoc[ii];
        }
    } else if (!isDynamic(sMode) && (opFlags[pflow_init_required])) {
        derivative(inputs, stateData, resid, sMode);
    }
}

void FmiMESubModel::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    updateLocalCache(inputs, stateData, sMode);
    if (isDynamic(sMode)) {
        me->getDerivatives(loc.destDiffLoc);
        /*     printf("tt=%f,I=%f, state=%f deriv=%e\n",
                    static_cast<double>(sD.time),
                    inputs[0],
                    loc.diffStateLoc[0],
                    loc.destDiffLoc[0]);*/
    } else {
        me->getDerivatives(loc.destLoc);
        /*      printf("tt=%f,I=%f, state=%f,deriv=%e\n",
                     static_cast<double>(sD.time),
                     inputs[0],
                     loc.algStateLoc[0],
                     loc.destLoc[0]);*/
    }
}

static constexpr double gap{1e-8};
double FmiMESubModel::getPartial(int depIndex, int refIndex, RefMode mode)
{
    double res{0.0};
    const double ich{1.0};
    const FmiVariableSet variableX = me->getVariableSet(depIndex);
    const FmiVariableSet variableY = me->getVariableSet(refIndex);
    if (opFlags[HAS_DERIVATIVE_FUNCTION]) {
        res = me->getPartialDerivative(depIndex, refIndex, ich);
    } else {
        double out1;
        double out2;
        double val1;
        double val2;
        fmi2Boolean evmd;
        fmi2Boolean term;

        me->get(variableX, &out1);
        me->get(variableY, &val1);
        val2 = val1 + gap;
        if (mode == RefMode::DIRECT) {
            me->set(variableY, &val2);
            me->get(variableX, &out2);
            me->set(variableY, &val1);
            res = (out2 - out1) / gap;
        } else if (mode == RefMode::LEVEL1) {
            me->set(variableY, &val2);
            me->getDerivatives(tempdState.data());
            me->get(variableX, &out2);
            me->set(variableY, &val1);
            me->getDerivatives(tempdState.data());
            res = (out2 - out1) / gap;
        } else if (mode == RefMode::LEVEL2) {
            me->getStates(tempState.data());
            tempState[refIndex] = val2;
            me->setStates(tempState.data());
            me->getDerivatives(tempdState.data());
            me->get(variableX, &out2);
            tempState[refIndex] = val1;
            me->setStates(tempState.data());
            me->getDerivatives(tempdState.data());
            res = (out2 - out1) / gap;
        } else if (mode == RefMode::LEVEL3) {
            // max useful for states dependent variables
            me->getStates(tempState.data());
            tempState[refIndex] = val2;
            me->setStates(tempState.data());
            me->completedIntegratorStep(fmi2False, &evmd, &term);
            me->getDerivatives(tempdState.data());

            me->get(variableX, &out2);
            tempState[refIndex] = val1;
            me->setStates(tempState.data());
            me->getDerivatives(tempdState.data());
            me->completedIntegratorStep(fmi2False, &evmd, &term);
            res = (out2 - out1) / gap;
        } else if (mode == RefMode::LEVEL4) {  // for input dependencies only
            me->set(variableY, &val2);
            me->completedIntegratorStep(fmi2False, &evmd, &term);
            me->get(variableX, &out2);
            me->set(variableY, &val1);
            me->completedIntegratorStep(fmi2False, &evmd, &term);
            res = (out2 - out1) / gap;
        } else if (mode == RefMode::LEVEL5) {  // for input dependencies only
            me->set(variableY, &val2);
            me->getStates(tempState.data());
            me->setStates(tempState.data());
            me->getDerivatives(tempdState.data());
            me->get(variableX, &out2);
            me->set(variableY, &val1);
            me->setStates(tempState.data());
            me->getDerivatives(tempdState.data());
            res = (out2 - out1) / gap;
        } else if (mode == RefMode::LEVEL7) {  // use the estimators
            if (opFlags[FIXED_OUTPUT_INTERVAL]) {
                res = 0;
            } else {
                res = oEst[depIndex]->stateDiff[refIndex];
            }
        } else if (mode == RefMode::LEVEL8) {  // use the estimators
            if (opFlags[FIXED_OUTPUT_INTERVAL]) {
                res = 0;
            } else {
                res = oEst[depIndex]->inputDiff[refIndex];  // TODO(PT):: this is wrong
            }
        }
    }
    return res;
}
void FmiMESubModel::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    if (hasDifferential(sMode)) {
        auto loc = offsets.getLocations(stateData, sMode, this);
        updateLocalCache(inputs, stateData, sMode);
        // for all the inputs
        for (index_t kk = 0; kk < loc.diffSize; ++kk) {
            const int variableIndex = stateInformation[kk].varIndex;
            for (int inputDependency : stateInformation[kk].inputDep) {
                double res = getPartial(variableIndex,
                                        inputVarIndices[inputDependency],
                                        stateInformation[kk].refMode);
                if (res != 0.0) {
                    matrixData.assign(loc.diffOffset + kk, inputLocs[inputDependency], res);
                }
            }
            for (int stateDependency : stateInformation[kk].stateDep) {
                double res = getPartial(variableIndex,
                                        stateInformation[stateDependency].varIndex,
                                        stateInformation[kk].refMode);
                if (res != 0.0) {
                    matrixData.assign(loc.diffOffset + kk, loc.diffOffset + stateDependency, res);
                }
            }
            matrixData.assign(loc.diffOffset + kk, loc.diffOffset + kk, -stateData.cj);
            /* this is not allowed in fmus
        for (auto &sR : varInfo[vu].derivDep)
        {
            vk = sR;
            res = getPartial(vu, vk);
            if (res != 0.0)
            {
                md.assign(Loc.diffOffset + kk, Loc.diffOffset + varInfo[vk].index, res*sD.cj);
            }
        }
        */
        }
    } else if (!isDynamic(sMode) && (opFlags[pflow_init_required])) {
        auto loc = offsets.getLocations(stateData, sMode, this);
        updateLocalCache(inputs, stateData, sMode);
        // for all the inputs
        for (index_t kk = 0; kk < m_stateSize; ++kk) {
            const int variableIndex = stateInformation[kk].varIndex;
            for (int inputDependency : stateInformation[kk].inputDep) {
                double res = getPartial(variableIndex,
                                        inputVarIndices[inputDependency],
                                        stateInformation[kk].refMode);
                if (res != 0.0) {
                    matrixData.assign(loc.algOffset + kk, inputLocs[inputDependency], res);
                }
            }
            for (int stateDependency : stateInformation[kk].stateDep) {
                double res = getPartial(variableIndex,
                                        stateInformation[stateDependency].varIndex,
                                        stateInformation[kk].refMode);
                if (res != 0.0) {
                    matrixData.assign(loc.algOffset + kk, loc.algOffset + kk, res);
                }
            }
        }
    }
}

void FmiMESubModel::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    CoreTime h = localIntegrationTime;
    // int sv = 0;
    // double aval = 0.95;
    // size_t aloc = 7;
    CoreTime curTime = prevTime;
    fmi2Boolean eventMode;
    fmi2Boolean terminateSim;
    CoreTime tend = time;
    std::vector<double> derX(m_stateSize);
    std::vector<double> derX2(m_stateSize);
    std::vector<double> prevInput(m_inputSize);
    std::vector<double> inputSlope(m_inputSize);
    // get the previous inputs
    me->getCurrentInputs(prevInput.data());
    // get the current states
    me->getStates(m_state.data());
    // compute the slopes of the inputs
    double h2 = 1.0 / (time - prevTime);
    for (index_t kk = 0; kk < m_inputSize; ++kk) {
        inputSlope[kk] = (inputs[kk] - prevInput[kk]) * h2;
    }
    while (curTime < tend) {
        // compute derivatives
        me->getDerivatives(derX.data());
        // advance time

        curTime += h;
        vectorMultAdd(prevInput, inputSlope, static_cast<double>(h), prevInput);
        me->setInputs(prevInput.data());
        me->setTime(curTime);
        // set states at t = time and perform one step
        vectorMultAdd(m_state, derX, static_cast<double>(h), m_state);
        me->setStates(m_state.data());

        // get event indicators at t = time
        me->completedIntegratorStep(fmi2False, &eventMode, &terminateSim);

        h = (curTime + h > tend) ? (tend - curTime) : localIntegrationTime;
    }
    prevTime = time;
}

void FmiMESubModel::ioPartialDerivatives(const IOdata& inputs,
                                         const StateData& sD,
                                         MatrixData<double>& md,
                                         const IOlocs& /*inputLocs*/,
                                         const SolverMode& sMode)
{
    updateLocalCache(inputs, sD, sMode);
    double ich = 1.0;

    for (index_t kk = 0; kk < m_outputSize; ++kk) {
        int vu = outputInformation[kk].varIndex;
        auto kmode = outputInformation[kk].refMode;
        if (kmode >= RefMode::LEVEL4) {
            if (isDynamic(sMode)) {
                kmode = RefMode::LEVEL8;
            }
        }
        for (auto& sR : outputInformation[kk].inputDep) {
            if (vu == inputVarIndices[sR]) {
                md.assign(kk, sR, ich);
            } else {
                double res = getPartial(vu, inputVarIndices[sR], kmode);
                if (res != 0.0) {
                    md.assign(kk, sR, res);
                }
            }
        }
    }
}

void FmiMESubModel::outputPartialDerivatives(const IOdata& inputs,
                                             const StateData& sD,
                                             MatrixData<double>& md,
                                             const SolverMode& sMode)
{
    auto loc = offsets.getLocations(sD, sMode, this);
    updateLocalCache(inputs, sD, sMode);
    auto offsetLoc = isDynamic(sMode) ? loc.diffOffset : loc.algOffset;

    for (index_t kk = 0; kk < m_outputSize; ++kk) {
        int vu = outputInformation[kk].varIndex;
        auto kmode = outputInformation[kk].refMode;
        if (kmode >= RefMode::LEVEL4) {
            if (isDynamic(sMode)) {
                kmode = RefMode::LEVEL7;
            }
        }
        if (outputInformation[kk].isState) {
            md.assign(kk, kk, 1.0);
        } else {
            for (auto& sR : outputInformation[kk].stateDep) {
                int vk = stateInformation[sR].varIndex;
                double res = getPartial(vu, vk, kmode);
                if (res != 0) {
                    md.assign(kk, offsetLoc + sR, res);
                }
            }
        }
    }
}

void FmiMESubModel::rootTest(const IOdata& inputs,
                             const StateData& sD,
                             double roots[],
                             const SolverMode& sMode)
{
    updateLocalCache(inputs, sD, sMode);
    auto rootOffset = offsets.getRootOffset(sMode);
    me->getEventIndicators(&(roots[rootOffset]));
}

void FmiMESubModel::rootTrigger(CoreTime /*time*/,
                                const IOdata& /*inputs*/,
                                const std::vector<int>& /*rootMask*/,
                                const SolverMode& /*sMode*/)
{
    me->setMode(FmuMode::EVENT_MODE);
    // TODO(PT): deal with the event
    me->setMode(FmuMode::CONTINUOUS_TIME_MODE);
}

IOdata FmiMESubModel::getOutputs(const IOdata& inputs,
                                 const StateData& sD,
                                 const SolverMode& sMode) const
{
    IOdata out(m_outputSize, 0);
    if (me->getCurrentMode() >= FmuMode::INITIALIZATION_MODE) {
        // updateInfo(inputs, sD, sMode);
        me->getOutputs(out.data());
        // printf("time=%f, out1 =%f, out 2=%f\n", static_cast<double>((!sD.empty()) ? sD.time :
        // prevTime), out[0], out[1]);
        if ((opFlags[USE_OUTPUT_ESTIMATOR]) && (!sD.empty()) && (!opFlags[FIXED_OUTPUT_INTERVAL]) &&
            (isDynamic(sMode))) {
            for (index_t pp = 0; pp < m_outputSize; ++pp) {
                if (outputInformation[pp].refMode >= RefMode::LEVEL4) {
                    const double res = oEst[pp]->estimate(sD.time,
                                                          inputs,
                                                          sD.state + offsets.getDiffOffset(sMode));
                    out[pp] = res;
                }
            }
        }
    }
    return out;
}

double FmiMESubModel::getDoutdt(const IOdata& /*inputs*/,
                                const StateData& /*sD*/,
                                const SolverMode& /*sMode*/,
                                index_t /*outputNum*/) const
{
    return 0;
}

double FmiMESubModel::getOutput(const IOdata& inputs,
                                const StateData& sD,
                                const SolverMode& sMode,
                                index_t outputNum) const
{
    double out = kNullVal;
    if (me->getCurrentMode() >= FmuMode::INITIALIZATION_MODE) {
        // updateInfo(inputs, sD, sMode);

        if ((opFlags[USE_OUTPUT_ESTIMATOR]) && (!sD.empty()) && (!opFlags[FIXED_OUTPUT_INTERVAL]) &&
            (isDynamic(sMode))) {
            if (outputInformation[outputNum].refMode >= RefMode::LEVEL4) {
                out = oEst[outputNum]->estimate(sD.time,
                                                inputs,
                                                sD.state + offsets.getDiffOffset(sMode));
            }
        } else {
            out = me->getOutput(outputNum);
        }
    }
    return out;
}

double FmiMESubModel::getOutput(index_t outputNum) const
{
    double out = kNullVal;
    if (me->getCurrentMode() >= FmuMode::INITIALIZATION_MODE) {
        out = me->getOutput(outputNum);
    }
    return out;
}

index_t FmiMESubModel::getOutputLoc(const SolverMode& /*sMode*/, index_t /*outputNum*/) const
{
    return kNullLocation;
}

void FmiMESubModel::updateLocalCache(const IOdata& inputs,
                                     const StateData& sD,
                                     const SolverMode& sMode)
{
    fmi2Boolean eventMode;
    fmi2Boolean terminateSim;
    if (!me) {
        return;
    }
    if (!sD.empty()) {
        if (sD.updateRequired(lastSeqID)) {
            auto loc = offsets.getLocations(sD, sMode, this);
            me->setTime(sD.time);
            if (m_stateSize > 0) {
                if (isDynamic(sMode)) {
                    me->setStates(loc.diffStateLoc);
                } else {
                    me->setStates(loc.algStateLoc);
                }
            }
            me->setInputs(inputs.data());
            lastSeqID = sD.seqID;
            if (m_stateSize > 0) {
                me->getDerivatives(tempdState.data());
            }
            if (!isDynamic(sMode)) {
                me->completedIntegratorStep(fmi2False, &eventMode, &terminateSim);
            }
        }
    } else if (!inputs.empty()) {
        me->setInputs(inputs.data());
        if (m_stateSize > 0) {
            me->getDerivatives(tempdState.data());
        }
        if (!isDynamic(sMode)) {
            me->completedIntegratorStep(fmi2False, &eventMode, &terminateSim);
        }
    }
}

void FmiMESubModel::makeSettableState()
{
    if (opFlags[dyn_initialized]) {
        prevFmiState = me->getCurrentMode();
        me->setMode(FmuMode::EVENT_MODE);
    }
}
void FmiMESubModel::resetState()
{
    if (opFlags[dyn_initialized]) {
        if (prevFmiState == me->getCurrentMode()) {
            return;
        }
        me->setMode(prevFmiState);
    }
}

void FmiMESubModel::probeFMU()
{
    RefMode defMode = (m_stateSize > 0) ? RefMode::LEVEL1 : RefMode::LEVEL4;

    if (opFlags[REPROBE_FLAG]) {
        defMode = (m_stateSize > 0) ? RefMode::DIRECT : RefMode::LEVEL4;
    }
    for (auto& stateInfo : stateInformation) {
        auto mode = RefMode::DIRECT;
        for (auto& dep : stateInfo.stateDep) {
            auto depIndex = stateInformation[dep].varIndex;
            double res = getPartial(stateInfo.varIndex, depIndex, RefMode::DIRECT);
            if (res != 0.0) {
                continue;
            }
            res = getPartial(stateInfo.varIndex, depIndex, RefMode::LEVEL1);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL1);
                continue;
            }
            res = getPartial(stateInfo.varIndex, depIndex, RefMode::LEVEL2);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL2);
                continue;
            }
            res = getPartial(stateInfo.varIndex, depIndex, RefMode::LEVEL3);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL3);
                continue;
            }
            mode = (std::max)(mode, defMode);
            opFlags.set(REPROBE_FLAG);
        }
        for (auto& dep : stateInfo.stateDep) {
            auto depIndex = stateInformation[dep].varIndex;
            double res = getPartial(stateInfo.varIndex, depIndex, RefMode::DIRECT);
            if (res != 0) {
                continue;
            }
            res = getPartial(stateInfo.varIndex, depIndex, RefMode::LEVEL1);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL1);
                continue;
            }
            mode = (std::max)(mode, defMode);
            opFlags.set(REPROBE_FLAG);
        }
        stateInfo.refMode = mode;
    }
    for (auto& outputInfo : outputInformation) {
        auto mode = RefMode::DIRECT;
        for (auto dep : outputInfo.stateDep) {
            auto depIndex = stateInformation[dep].varIndex;
            double res = getPartial(outputInfo.varIndex, depIndex, RefMode::DIRECT);
            if (res != 0) {
                continue;
            }
            res = getPartial(outputInfo.varIndex, depIndex, RefMode::LEVEL1);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL1);
                continue;
            }
            res = getPartial(outputInfo.varIndex, depIndex, RefMode::LEVEL2);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL2);
                continue;
            }
            res = getPartial(outputInfo.varIndex, depIndex, RefMode::LEVEL4);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL4);
                continue;
            }
            res = getPartial(outputInfo.varIndex, depIndex, RefMode::LEVEL5);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL5);
                continue;
            }
            mode = (std::max)(mode, defMode);
            opFlags.set(REPROBE_FLAG);
        }
        for (auto& dep : outputInfo.inputDep) {
            auto depIndex = stateInformation[dep].varIndex;
            double res = getPartial(outputInfo.varIndex, depIndex, RefMode::DIRECT);
            if (res != 0) {
                continue;
            }
            if (m_stateSize > 0) {
                res = getPartial(outputInfo.varIndex, depIndex, RefMode::LEVEL1);
                if (res != 0) {
                    mode = (std::max)(mode, RefMode::LEVEL1);
                    continue;
                }
            }
            res = getPartial(outputInfo.varIndex, depIndex, RefMode::LEVEL4);
            if (res != 0) {
                mode = (std::max)(mode, RefMode::LEVEL4);
                continue;
            }
            if (m_stateSize > 0) {
                res = getPartial(outputInfo.varIndex, depIndex, RefMode::LEVEL5);
                if (res != 0) {
                    mode = (std::max)(mode, RefMode::LEVEL5);
                    continue;
                }
            }
            mode = (std::max)(mode, defMode);
            opFlags.set(REPROBE_FLAG);
        }
        outputInfo.refMode = mode;
        if (mode >= RefMode::LEVEL4) {
            opFlags.set(USE_OUTPUT_ESTIMATOR);
            std::vector<int> sDep(outputInfo.stateDep.size());
            std::vector<int> iDep(outputInfo.inputDep.size());
            for (size_t dd = 0; dd < outputInfo.stateDep.size(); ++dd) {
                sDep[dd] = outputInfo.stateDep[dd];
            }
            for (size_t dd = 0; dd < outputInfo.inputDep.size(); ++dd) {
                iDep[dd] = outputInfo.inputDep[dd];
            }
            oEst[outputInfo.index] = new OutputEstimator(sDep, iDep);
        }
    }
}

void FmiMESubModel::loadOutputJac(int index)
{
    double pd;
    int ct = 0;
    if (index == -1) {
        for (auto& out : outputInformation) {
            if (out.refMode >= RefMode::LEVEL4) {
                ct = 0;
                for (auto kk : out.stateDep) {
                    pd = getPartial(out.varIndex, stateInformation[kk].varIndex, out.refMode);
                    oEst[out.index]->stateDiff[ct] = pd;
                    ++ct;
                }
                ct = 0;
                for (auto kk : out.inputDep) {
                    pd = getPartial(out.varIndex, inputVarIndices[kk], out.refMode);
                    oEst[out.index]->inputDiff[ct] = pd;
                    ++ct;
                }
            }
        }
    } else {
        if (outputInformation[index].refMode >= RefMode::LEVEL4) {
            ct = 0;
            for (auto kk : outputInformation[index].stateDep) {
                pd = getPartial(outputInformation[index].varIndex,
                                stateInformation[kk].varIndex,
                                outputInformation[index].refMode);
                oEst[outputInformation[index].index]->stateDiff[ct] = pd;
                ++ct;
            }
            ct = 0;
            for (auto kk : outputInformation[index].inputDep) {
                pd = getPartial(outputInformation[index].varIndex,
                                inputVarIndices[kk],
                                outputInformation[index].refMode);
                oEst[outputInformation[index].index]->inputDiff[ct] = pd;
                ++ct;
            }
        }
    }
}

// NOLINTEND(readability-identifier-length,misc-const-correctness)
}  // namespace griddyn::fmi
