/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiCoSimSubModel.h"

#include "../fmi_import/fmiLibraryManager.h"
#include "../fmi_import/fmiObjects.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "outputEstimator.h"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
namespace fmi {
    static const bool unimplemented = false;

    fmiCoSimSubModel::fmiCoSimSubModel(const std::string& newName,
                                       std::shared_ptr<fmi2CoSimObject> fmi):
        gridSubModel(newName), cs(std::move(fmi))
    {
    }

    fmiCoSimSubModel::fmiCoSimSubModel(std::shared_ptr<fmi2CoSimObject> fmi): cs(std::move(fmi)) {}

    fmiCoSimSubModel::~fmiCoSimSubModel() = default;

    coreObject* fmiCoSimSubModel::clone(coreObject* obj) const
    {
        auto* gco = cloneBase<fmiCoSimSubModel, gridSubModel>(this, obj);
        if (gco == nullptr) {
            return obj;
        }

        return gco;
    }

    bool fmiCoSimSubModel::isLoaded() const
    {
        return static_cast<bool>(cs);
    }

    void fmiCoSimSubModel::dynObjectInitializeA(coreTime time, std::uint32_t flags)
    {
        if (CHECK_CONTROLFLAG(force_constant_pflow_initialization, flags)) {
            opFlags.set(pflow_init_required);
        }
        prevTime = time;
    }

    void fmiCoSimSubModel::dynObjectInitializeB(const IOdata& inputs,
                                                const IOdata& /*desiredOutput*/,
                                                IOdata& /*fieldSet*/)
    {
        if (opFlags[pflow_init_required]) {
            if (opFlags[pFlow_initialized]) {
                /*
            cs->getStates(m_state.data());
            cs->setTime(prevTime - 0.01);

            if (opFlags[use_output_estimator])
            {
            //  if we require the use of output estimators flag that to the simulation and load the
            information
            // for the estimator
            alert(this, SINGLE_STEP_REQUIRED);
            double val;
            loadOutputJac();
            for (size_t pp = 0; pp < m_outputSize; ++pp)
                {
                    if (outputInformation[pp].refMode >= refMode_t::level4)
                    {
                        val = cs->getOutput(pp);
                        oEst[pp]->update(prevTime, val, inputs, m_state.data());
                    }
                }

            }
            */
                opFlags.set(dyn_initialized);
            } else {  // in pflow mode
                cs->setMode(fmuMode::initializationMode);

                cs->setInputs(inputs.data());
                cs->setMode(fmuMode::continuousTimeMode);
                estimators.resize(m_outputSize);
                // probeFMU();
                opFlags.set(pFlow_initialized);
            }
        } else {
            assert(unimplemented);
            /*
        cs->setMode (fmuMode::initializationMode);

        cs->setInputs (inputs.data ());
        cs->setMode (fmuMode::continuousTimeMode);

        cs->getStates (m_state.data ());
        oEst.resize (m_outputSize);
        probeFMU ();  // probe the fmu
        if (opFlags[use_output_estimator])
        {
            // if we require the use of output estimators flag that to the simulation and load the
        information
            // for the estimator
            alert (this, SINGLE_STEP_REQUIRED);
            loadOutputJac ();
        }
        cs->setTime (prevTime - 0.01);
        */
        }
    }

    static const std::string paramString("params");
    static const std::string inputString("inputs");

    void fmiCoSimSubModel::getParameterStrings(stringVec& pstr, paramStringType pstype) const
    {
        int strpcnt = 0;
        const auto* info = cs->fmuInformation();
        auto vcnt = info->getCounts("variables");
        switch (pstype) {
            case paramStringType::all:
                pstr.reserve(pstr.size() + info->getCounts(paramString) +
                             info->getCounts(inputString) - m_inputSize);

                for (int kk = 0; kk < vcnt; ++kk) {
                    if (info->getVariableInfo(kk).type == fmi_variable_type_t::string) {
                        ++strpcnt;
                    } else if (checkType(info->getVariableInfo(kk),
                                         fmi_variable_type_t::numeric,
                                         fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }

                gridSubModel::getParameterStrings(pstr, paramStringType::numeric);
                pstr.reserve(pstr.size() + strpcnt + 1);
                pstr.push_back("#");
                for (int kk = 0; kk < vcnt; ++kk) {
                    if (checkType(info->getVariableInfo(kk),
                                  fmi_variable_type_t::string,
                                  fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }
                gridSubModel::getParameterStrings(pstr, paramStringType::str);
                break;
            case paramStringType::localnum:
                pstr.reserve(info->getCounts(paramString) + info->getCounts(inputString) -
                             m_inputSize);
                pstr.resize(0);
                for (int kk = 0; kk < vcnt; ++kk) {
                    if (checkType(info->getVariableInfo(kk),
                                  fmi_variable_type_t::numeric,
                                  fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }
                break;
            case paramStringType::localstr:
                pstr.reserve(info->getCounts(paramString) + info->getCounts(inputString) -
                             m_inputSize);
                pstr.resize(0);
                for (int kk = 0; kk < vcnt; ++kk) {
                    if (checkType(info->getVariableInfo(kk),
                                  fmi_variable_type_t::string,
                                  fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }
                break;
            case paramStringType::localflags:
                pstr.reserve(info->getCounts(paramString) + info->getCounts(inputString) -
                             m_inputSize);
                pstr.resize(0);
                for (int kk = 0; kk < vcnt; ++kk) {
                    if (checkType(info->getVariableInfo(kk),
                                  fmi_variable_type_t::boolean,
                                  fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }
                break;
            case paramStringType::numeric:
                pstr.reserve(pstr.size() + info->getCounts(paramString) +
                             info->getCounts(inputString) - m_inputSize);
                for (int kk = 0; kk < vcnt; ++kk) {
                    if (checkType(info->getVariableInfo(kk),
                                  fmi_variable_type_t::numeric,
                                  fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }
                gridSubModel::getParameterStrings(pstr, paramStringType::numeric);
                break;
            case paramStringType::str:
                pstr.reserve(pstr.size() + info->getCounts(paramString) +
                             info->getCounts(inputString) - m_inputSize);
                for (int kk = 0; kk < vcnt; ++kk) {
                    if (checkType(info->getVariableInfo(kk),
                                  fmi_variable_type_t::string,
                                  fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }
                gridSubModel::getParameterStrings(pstr, paramStringType::str);
                break;
            case paramStringType::flags:
                pstr.reserve(pstr.size() + info->getCounts(paramString) +
                             info->getCounts(inputString) - m_inputSize);
                for (int kk = 0; kk < vcnt; ++kk) {
                    if (checkType(info->getVariableInfo(kk),
                                  fmi_variable_type_t::boolean,
                                  fmi_causality_type_t::parameter)) {
                        pstr.push_back(info->getVariableInfo(kk).name);
                    }
                }
                gridSubModel::getParameterStrings(pstr, paramStringType::flags);
                break;
        }
    }

    stringVec fmiCoSimSubModel::getOutputNames() const
    {
        return cs->getOutputNames();
    }

    stringVec fmiCoSimSubModel::getInputNames() const
    {
        return cs->getInputNames();
    }

    void fmiCoSimSubModel::set(const std::string& param, const std::string& val)
    {
        using gmlc::utilities::stringOps::splitline;
        using gmlc::utilities::stringOps::trim;

        if ((param == "fmu") || (param == "file")) {
            if (!(cs)) {
                cs = fmiLibraryManager::instance().createCoSimulationObject(val, getName());
            } else {
                // return INVALID_PARAMETER_VALUE;
                return;
            }
        } else if (param == "outputs") {
            auto ssep = splitline(val);
            trim(ssep);
            cs->setOutputVariables(ssep);
            m_outputSize = cs->outputSize();
        } else if (param == inputString) {
            auto ssep = splitline(val);
            trim(ssep);
            cs->setInputVariables(ssep);
            m_inputSize = cs->inputSize();
            //    updateDependencyInfo();
        } else {
            bool isparam = cs->isParameter(param, fmi_variable_type_t::string);
            if (isparam) {
                makeSettableState();
                cs->set(param, val);
                resetState();
            } else {
                gridSubModel::set(param, val);
            }
        }
    }
    static const std::string localIntegrationtimeString("localintegrationtime");
    void fmiCoSimSubModel::set(const std::string& param, double val, units::unit unitType)
    {
        if ((param == "timestep") || (param == localIntegrationtimeString)) {
            localIntegrationTime = val;
        } else {
            bool isparam = cs->isParameter(param, fmi_variable_type_t::numeric);
            if (isparam) {
                makeSettableState();
                cs->set(param, val);
                resetState();
            } else {
                gridSubModel::set(param, val, unitType);
            }
        }
    }

    double fmiCoSimSubModel::get(const std::string& param, units::unit unitType) const
    {
        if (param == localIntegrationtimeString) {
            return localIntegrationTime;
        }
        if (cs->isVariable(param, fmi_variable_type_t::numeric)) {
            return cs->get<double>(param);
        }
        return gridSubModel::get(param, unitType);
    }

    double fmiCoSimSubModel::getPartial(int depIndex, int refIndex, refMode_t /*mode*/)
    {
        double res = 0.0;
        double ich = 1.0;
        fmiVariableSet vx = cs->getVariableSet(depIndex);
        fmiVariableSet vy = cs->getVariableSet(refIndex);
        if (opFlags[has_derivative_function]) {
            res = cs->getPartialDerivative(depIndex, refIndex, ich);
        } else {
            assert(unimplemented);
            /*
        const double gap = 1e-8;
        double out1, out2;
        double val1, val2;
        fmi2Boolean evmd;
        fmi2Boolean term;

        cs->get (vx, &out1);
        cs->get (vy, &out1);
        val2 = val1 + gap;
        if (mode == refMode_t::direct)
        {
            cs->set (vy, &val2);
            cs->get (vx, &out2);
            cs->set (vy, &val1);
            res = (out2 - out1) / gap;
        }
        else if (mode == refMode_t::level1)
        {
            cs->set (vy, &val2);
            cs->getDerivatives (tempdState.data ());
            cs->get (vx, &out2);
            cs->set (vy, &val1);
            cs->getDerivatives (tempdState.data ());
            res = (out2 - out1) / gap;
        }
        else if (mode == refMode_t::level2)
        {
            cs->getStates (tempState.data ());
            tempState[refIndex] = val2;
            cs->setStates (tempState.data ());
            cs->getDerivatives (tempdState.data ());
            cs->get (vx, &out2);
            tempState[refIndex] = val1;
            cs->setStates (tempState.data ());
            cs->getDerivatives (tempdState.data ());
            res = (out2 - out1) / gap;
        }
        else if (mode == refMode_t::level3)  // max useful for states dependent variables
        {
            cs->getStates (tempState.data ());
            tempState[refIndex] = val2;
            cs->setStates (tempState.data ());
            cs->completedIntegratorStep (false, &evmd, &term);
            cs->getDerivatives (tempdState.data ());

            cs->get (vx, &out2);
            tempState[refIndex] = val1;
            cs->setStates (tempState.data ());
            cs->getDerivatives (tempdState.data ());
            cs->completedIntegratorStep (false, &evmd, &term);
            res = (out2 - out1) / gap;
        }
        else if (mode == refMode_t::level4)  // for input dependencies only
        {
            cs->set (vy, &val2);
            cs->completedIntegratorStep (false, &evmd, &term);
            cs->get (vx, &out2);
            cs->set (vy, &val1);
            cs->completedIntegratorStep (false, &evmd, &term);
            res = (out2 - out1) / gap;
        }
        else if (mode == refMode_t::level5)  // for input dependencies only
        {
            cs->set (vy, &val2);
            cs->getStates (tempState.data ());
            cs->setStates (tempState.data ());
            cs->getDerivatives (tempdState.data ());
            cs->get (vx, &out2);
            cs->set (vy, &val1);
            cs->setStates (tempState.data ());
            cs->getDerivatives (tempdState.data ());
            res = (out2 - out1) / gap;
        }
        else if (mode == refMode_t::level7)  // use the estimators
        {
            if (opFlags[fixed_output_interval])
            {
                res = 0;
            }
            else
            {
                res = oEst[depIndex]->stateDiff[refIndex];
            }
        }
        else if (mode == refMode_t::level8)  // use the estimators
        {
            if (opFlags[fixed_output_interval])
            {
                res = 0;
            }
            else
            {
                res = oEst[depIndex]->inputDiff[refIndex];  // TODO:: this is wrong
            }

        }
        */
        }
        return res;
    }

    void fmiCoSimSubModel::timestep(coreTime /*time*/,
                                    const IOdata& /*inputs*/,
                                    const solverMode& /*sMode*/)
    {
        assert(unimplemented);
        /*
    double h = localIntegrationTime;
    int sv = 0;
    double aval = 0.95;
    size_t aloc = 7;
    double time = prevTime;
    fmi2Boolean eventMode;
    fmi2Boolean terminateSim;
    double Tend = time;
    std::vector<double> der_x (m_stateSize);
    std::vector<double> der_x2 (m_stateSize);
    std::vector<double> prevInput (m_inputSize);
    std::vector<double> inputSlope (m_inputSize);
    // get the previous inputs
    cs->getCurrentInputs (prevInput.data ());
    // get the current states
    cs->getStates (m_state.data ());
    // compute the slopes of the inputs
    for (size_t kk = 0; kk < m_inputSize; ++kk)
    {
        inputSlope[kk] = (inputs[kk] - prevInput[kk]) / (time - prevTime);
    }
    while (time < Tend)
    {
        // compute derivatives
        cs->getDerivatives (der_x.data ());
        // advance time

        time = time + h;
        vectorMultAdd (prevInput, inputSlope, h, prevInput);
        cs->setInputs (prevInput.data ());
        cs->setTime (time);
        // set states at t = time and perform one step
        vectorMultAdd (m_state, der_x, h, m_state);
        cs->setStates (m_state.data ());

        // get event indicators at t = time
        cs->completedIntegratorStep (false, &eventMode, &terminateSim);

        h = (time + h > Tend) ? (Tend - time) : localIntegrationTime;
    }
    prevTime = time;
    double out = cs->getOutput (0);

    return out;
    */
    }

    void fmiCoSimSubModel::ioPartialDerivatives(const IOdata& /*inputs*/,
                                                const stateData& /*sD*/,
                                                matrixData<double>& /* md*/,
                                                const IOlocs& /*inputLocs*/,
                                                const solverMode& /*sMode*/)
    {
        assert(unimplemented);
        /*
    updateInfo (inputs, sD, sMode);
    double res;
    double ich = 1.0;
    index_t kk;
    int vk, vu;

    for (kk = 0; kk < m_outputSize; ++kk)
    {
        vu = outputInformation[kk].varIndex;
        auto kmode = outputInformation[kk].refMode;
        if (kmode >= refMode_t::level4)
        {
            if (isDynamic (sMode))
            {
                kmode = refMode_t::level8;
            }
        }
        for (auto &sR : outputInformation[kk].inputDep)
        {
            if (vu == inputVarIndices[sR])
            {
                md.assign (kk, sR, 1.0);
            }
            else
            {
                vk = sR;
                res = getPartial (vu, inputVarIndices[sR], kmode);
                if (res != 0.0)
                {
                    md.assign (kk, sR, res);
                }
            }
        }
    }
    */
    }

    IOdata fmiCoSimSubModel::getOutputs(const IOdata& /*inputs*/,
                                        const stateData& sD,
                                        const solverMode& sMode) const
    {
        IOdata out(m_outputSize, 0);
        if (cs->getCurrentMode() >= fmuMode::initializationMode) {
            // updateInfo(inputs, sD, sMode);
            cs->getOutputs(out.data());
            /*   printf("time=%f, out1 =%f, out 2=%f\n",
                      static_cast<double>((!sD.empty()) ? sD.time : prevTime),
                      out[0],
                      out[1]);
                      */
            if ((opFlags[use_output_estimator]) && (!sD.empty()) &&
                (!opFlags[fixed_output_interval]) && (isDynamic(sMode))) {
                for (index_t pp = 0; pp < m_outputSize; ++pp) {
                    /*
                if (outputInformation[pp].refMode >= refMode_t::level4)
                {
                    const double res = oEst[pp]->estimate(sD.time, inputs, sD.state +
                        offsets.getDiffOffset(sMode)); out[pp] = res;
                }
                */
                }
            }
        }
        return out;
    }

    double fmiCoSimSubModel::getDoutdt(const IOdata& /*inputs*/,
                                       const stateData& /*sD*/,
                                       const solverMode& /*sMode*/,
                                       index_t /*outputNum*/) const
    {
        return 0;
    }

    double fmiCoSimSubModel::getOutput(const IOdata& /*inputs*/,
                                       const stateData& sD,
                                       const solverMode& sMode,
                                       index_t outputNum) const
    {
        double out = kNullVal;
        if (cs->getCurrentMode() >= fmuMode::initializationMode) {
            // updateInfo(inputs, sD, sMode);

            if ((opFlags[use_output_estimator]) && (!sD.empty()) &&
                (!opFlags[fixed_output_interval]) && (isDynamic(sMode))) {
                /*
            if (outputInformation[num].refMode >= refMode_t::level4)
            {
                out = oEst[num]->estimate(sD.time, inputs, sD.state + offsets.getDiffOffset(sMode));
            }
            */
            } else {
                out = cs->getOutput(outputNum);
            }
        }
        return out;
    }

    double fmiCoSimSubModel::getOutput(index_t outputNum) const
    {
        double out = kNullVal;
        if (cs->getCurrentMode() >= fmuMode::initializationMode) {
            out = cs->getOutput(outputNum);
        }
        return out;
    }

    void fmiCoSimSubModel::updateLocalCache(const IOdata& inputs,
                                            const stateData& sD,
                                            const solverMode& sMode)
    {
        /*
    fmi2Boolean eventMode;
    fmi2Boolean terminateSim;
    if (!sD.empty())
    {
        if ((sD.seqID == 0) || (sD.seqID != lastSeqID))
        {
            auto Loc = offsets.getLocations(sD, sMode, this);
            cs->setTime(sD.time);
            if (m_stateSize > 0)
            {
                if (isDynamic(sMode))
                {
                    cs->setStates(Loc.diffStateLoc);
                }
                else
                {
                    cs->setStates(Loc.algStateLoc);
                }
            }
            cs->setInputs(inputs.data());
            lastSeqID = sD.seqID;
            if (m_stateSize > 0)
            {
                cs->getDerivatives(tempdState.data());
            }
            if (!isDynamic(sMode))
            {
                cs->completedIntegratorStep(false, &eventMode, &terminateSim);
            }

        }
    }
    else if (!inputs.empty())
    {
        cs->setInputs(inputs.data());
        if (m_stateSize > 0)
        {
            cs->getDerivatives(tempdState.data());
        }
        if (!isDynamic(sMode))
        {
            cs->completedIntegratorStep(false, &eventMode, &terminateSim);
        }
    }
    */
    }

    void fmiCoSimSubModel::makeSettableState()
    {
        if (opFlags[dyn_initialized]) {
            // prevFmiState = cs->getCurrentMode();
            cs->setMode(fmuMode::eventMode);
        }
    }
    void fmiCoSimSubModel::resetState()
    {
        if (opFlags[dyn_initialized]) {
            // if (prevFmiState == cs->getCurrentMode())
            {
                return;
            }
            // cs->setMode(prevFmiState);
        }
    }

    void fmiCoSimSubModel::loadOutputJac(int index)  // NOLINT
    {
        // double pd;
        // int ct = 0;
        if (index == -1) {  // NOLINT
            /*
        for (auto &out : outputInformation)
        {
            if (out.refMode >= refMode_t::level4)
            {
                ct = 0;
                for (auto kk : out.stateDep)
                {
                    pd = getPartial(out.varIndex, stateInformation[kk].varIndex, out.refMode);
                    oEst[out.index]->stateDiff[ct] = pd;
                    ++ct;
                }
                ct = 0;
                for (auto kk : out.inputDep)
                {
                    pd = getPartial(out.varIndex, inputVarIndices[kk], out.refMode);
                    oEst[out.index]->inputDiff[ct] = pd;
                    ++ct;
                }
            }
        }
        */
        } else {  // NOLINT
            /*
        if (outputInformation[index].refMode >= refMode_t::level4)
        {
            ct = 0;
            for (auto kk : outputInformation[index].stateDep)
            {
                pd = getPartial(outputInformation[index].varIndex, stateInformation[kk].varIndex,
                    outputInformation[index].refMode);
                oEst[outputInformation[index].index]->stateDiff[ct] = pd;
                ++ct;
            }
            ct = 0;
            for (auto kk : outputInformation[index].inputDep)
            {
                pd = getPartial(outputInformation[index].varIndex, inputVarIndices[kk],
                    outputInformation[index].refMode);
                oEst[outputInformation[index].index]->inputDiff[ct] = pd;
                ++ct;
            }
        }
        */
        }
    }

}  // namespace fmi
}  // namespace griddyn
