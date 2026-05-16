/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiObjects.h"
#include <cstdio>
#include <memory>
#include <print>
#include <string>
#include <utility>
#include <vector>

Fmi2Object::Fmi2Object(fmi2Component cmp,
                       std::shared_ptr<const fmiInfo> keyInfo,
                       std::shared_ptr<const fmiCommonFunctions> comFunc):
    comp(cmp), info(std::move(keyInfo)), commonFunctions(std::move(comFunc))
{
}

Fmi2Object::~Fmi2Object()
{
    commonFunctions->fmi2FreeInstance(comp);
}
void Fmi2Object::setupExperiment(fmi2Boolean toleranceDefined,
                                 fmi2Real tolerance,
                                 fmi2Real startTime,
                                 fmi2Boolean stopTimeDefined,
                                 fmi2Real stopTime)
{
    auto ret = commonFunctions->fmi2SetupExperiment(
        comp, toleranceDefined, tolerance, startTime, stopTimeDefined, stopTime);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

FmuMode Fmi2Object::getCurrentMode() const
{
    return currentMode;
}

void Fmi2Object::setMode(FmuMode newMode)
{
    if (newMode == FmuMode::error) {
        currentMode = FmuMode::error;
        throw(FmiErrorException());
    }
    fmi2Status ret = fmi2Error;
    if (newMode == FmuMode::terminated) {
        currentMode = FmuMode::terminated;
        ret = commonFunctions->fmi2Terminate(comp);
    }

    switch (currentMode) {
        case FmuMode::instantiatedMode:
            switch (newMode) {
                case FmuMode::initializationMode:
                    if (inputSize() == 0) {
                        setDefaultInputs();
                    }
                    if (outputSize() == 0) {
                        setDefaultOutputs();
                    }
                    // printf("calling fmi enterInitMode\n");
                    ret = commonFunctions->fmi2EnterInitializationMode(comp);
                    // printf("return from fmiEnterInitializationMode with code %d\n", ret);
                    break;
                case FmuMode::eventMode:
                case FmuMode::stepMode:
                    Fmi2Object::setMode(
                        FmuMode::initializationMode);  // go into initialization first
                    ret = commonFunctions->fmi2ExitInitializationMode(comp);
                    break;
                default:
                    break;
            }
            break;
        case FmuMode::initializationMode:
            if ((newMode == FmuMode::eventMode) || (newMode == FmuMode::stepMode)) {
                // printf("calling exit initMode\n");
                if (!commonFunctions->fmi2ExitInitializationMode) {
                    std::println("function is not present");
                }
                ret = commonFunctions->fmi2ExitInitializationMode(comp);
                // printf("finished exit initMode return %d\n", ret);
            }
            break;
        default:
            break;
    }

    if (ret == fmi2OK) {
        currentMode = newMode;
    } else if (currentMode == newMode) {
        ret = fmi2OK;
    }
    handleNonOKReturnValues(ret);
}

void Fmi2Object::reset()
{
    currentMode = FmuMode::instantiatedMode;
    auto ret = commonFunctions->fmi2Reset(comp);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

template<>
std::string Fmi2Object::get<std::string>(const std::string& param) const
{
    auto ref = info->getVariableInfo(param);
    if (ref.type != FmiVariableType::string) {
        handleNonOKReturnValues(fmi2Status::fmi2Discard);
        return "";  // if we get here just return an empty string otherwise we threw an exception
    }
    fmi2String res;
    auto retval = commonFunctions->fmi2GetString(comp, &(ref.valueRef), 1, &res);
    if (retval != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(retval);
    }
    return std::string(res);  // this should copy the actual the string
}

void Fmi2Object::get(const FmiVariableSet& vrset, fmi2Real value[]) const
{
    auto ret = commonFunctions->fmi2GetReal(comp, vrset.getValueRef(), vrset.getVRcount(), value);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2Object::get(const FmiVariableSet& vrset, fmi2Integer value[]) const
{
    auto ret =
        commonFunctions->fmi2GetInteger(comp, vrset.getValueRef(), vrset.getVRcount(), value);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::get(const FmiVariableSet& vrset, fmi2String value[]) const
{
    auto ret = commonFunctions->fmi2GetString(comp, vrset.getValueRef(), vrset.getVRcount(), value);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::set(const FmiVariableSet& vrset, fmi2Integer value[])
{
    auto ret =
        commonFunctions->fmi2GetInteger(comp, vrset.getValueRef(), vrset.getVRcount(), value);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::set(const FmiVariableSet& vrset, fmi2Real value[])
{
    auto retval =
        commonFunctions->fmi2SetReal(comp, vrset.getValueRef(), vrset.getVRcount(), value);
    if (retval != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(retval);
    }
}

void Fmi2Object::set(const std::string& param, const char* val)
{
    auto ref = info->getVariableInfo(param);
    if (!(ref.type == FmiVariableType::string)) {
        handleNonOKReturnValues(fmi2Status::fmi2Discard);
        return;
    }
    auto ret = commonFunctions->fmi2SetString(comp, &(ref.valueRef), 1, &val);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::set(const std::string& param, const std::string& val)
{
    auto ref = info->getVariableInfo(param);
    if (!(ref.type == FmiVariableType::string)) {
        handleNonOKReturnValues(fmi2Status::fmi2Discard);
        return;
    }
    fmi2String val2 = val.c_str();
    auto ret = commonFunctions->fmi2SetString(comp, &(ref.valueRef), 1, &val2);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::setFlag(const std::string& param, bool val)
{
    auto ref = info->getVariableInfo(param);
    if (!(ref.type == FmiVariableType::string)) {
        handleNonOKReturnValues(fmi2Status::fmi2Discard);
        return;
    }
    fmi2Boolean val2 = val ? fmi2True : fmi2False;
    auto ret = commonFunctions->fmi2SetBoolean(comp, &(ref.valueRef), 1, &val2);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::getFMUState(fmi2FMUstate* FMUState)
{
    auto ret = commonFunctions->fmi2GetFMUstate(comp, FMUState);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2Object::setFMUState(fmi2FMUstate FMUState)
{
    auto ret = commonFunctions->fmi2SetFMUstate(comp, FMUState);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

size_t Fmi2Object::serializedStateSize(fmi2FMUstate FMUState)
{
    size_t sz;
    auto ret = commonFunctions->fmi2SerializedFMUstateSize(comp, FMUState, &sz);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
    return sz;
}
void Fmi2Object::serializeState(fmi2FMUstate FMUState, fmi2Byte serializedState[], size_t size)
{
    auto ret = commonFunctions->fmi2SerializeFMUstate(comp, FMUState, serializedState, size);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2Object::deSerializeState(const fmi2Byte serializedState[],
                                  size_t size,
                                  fmi2FMUstate* FMUState)
{
    auto ret = commonFunctions->fmi2DeSerializeFMUstate(comp, serializedState, size, FMUState);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2Object::getDirectionalDerivative(const fmi2ValueReference vUnknown_ref[],
                                          size_t nUnknown,
                                          const fmi2ValueReference vKnown_ref[],
                                          size_t unknown,
                                          const fmi2Real dvKnown[],
                                          fmi2Real dvUnknown[])
{
    auto ret = commonFunctions->fmi2GetDirectionalDerivative(
        comp, vUnknown_ref, nUnknown, vKnown_ref, unknown, dvKnown, dvUnknown);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

fmi2Real Fmi2Object::getPartialDerivative(int index_x, int index_y, double dx)
{
    double dy;
    commonFunctions->fmi2GetDirectionalDerivative(comp,
                                                  &(info->getVariableInfo(index_x).valueRef),
                                                  1,
                                                  &(info->getVariableInfo(index_y).valueRef),
                                                  1,
                                                  &dx,
                                                  &dy);
    return dy;
}

/** check if an output is real and actually is an output*/
bool isRealOutput(const VariableInformation& vI)
{
    return ((vI.index >= 0) && (vI.type == FmiVariableType::real) &&
            (vI.causality == FmiCausalityType::output));
}

/** check if an input is real and actually is an input*/
bool isRealInput(const VariableInformation& vI)
{
    return ((vI.index >= 0) && (vI.type == FmiVariableType::real) &&
            (vI.causality == FmiCausalityType::input));
}

void Fmi2Object::setOutputVariables(const std::vector<std::string>& outNames)
{
    if (outNames.size() == 1) {
        if (outNames[0] == "all") {
            setDefaultOutputs();
            return;
        }
    }
    activeOutputs.clear();
    activeOutputIndices.clear();
    for (auto& outName : outNames) {
        auto& vI = info->getVariableInfo(outName);
        if (isRealOutput(vI)) {
            activeOutputs.push(vI.valueRef);
            activeOutputIndices.push_back(vI.index);
        }
        // TODO(phlpt): Decide what to do if this condition is not valid.
    }
}

void Fmi2Object::setOutputVariables(const std::vector<int>& outIndices)
{
    activeOutputIndices.clear();
    activeOutputs.clear();
    for (auto& outIndex : outIndices) {
        auto& vI = info->getVariableInfo(outIndex);
        if (isRealOutput(vI)) {
            activeOutputs.push(vI.valueRef);
            activeOutputIndices.push_back(vI.index);
        }
    }
}

void Fmi2Object::setInputVariables(const std::vector<std::string>& inNames)
{
    if (inNames.size() == 1) {
        if (inNames[0] == "all") {
            setDefaultInputs();
            return;
        }
    }
    activeInputs.clear();
    activeInputIndices.clear();
    for (auto& inName : inNames) {
        auto& vI = info->getVariableInfo(inName);
        if (isRealInput(vI)) {
            activeInputs.push(vI.valueRef);
            activeInputIndices.push_back(vI.index);
        }
    }
}

void Fmi2Object::setInputVariables(const std::vector<int>& inIndices)
{
    activeInputs.clear();
    activeInputIndices.clear();
    for (auto& inIndex : inIndices) {
        auto& vI = info->getVariableInfo(inIndex);
        if (isRealInput(vI)) {
            activeInputs.push(vI.valueRef);
            activeInputIndices.push_back(vI.index);
        }
    }
}

void Fmi2Object::setDefaultInputs()
{
    setInputVariables(info->getVariableNames("input"));
}

void Fmi2Object::setDefaultOutputs()
{
    setOutputVariables(info->getVariableNames("output"));
}

void Fmi2Object::setInputs(const fmi2Real inputs[])
{
    auto ret = commonFunctions->fmi2SetReal(comp,
                                            activeInputs.getValueRef(),
                                            activeInputs.getVRcount(),
                                            inputs);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::getCurrentInputs(fmi2Real inputs[])
{
    auto ret = commonFunctions->fmi2GetReal(comp,
                                            activeInputs.getValueRef(),
                                            activeInputs.getVRcount(),
                                            inputs);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2Object::getOutputs(fmi2Real outputs[]) const
{
    auto ret = commonFunctions->fmi2GetReal(comp,
                                            activeOutputs.getValueRef(),
                                            activeOutputs.getVRcount(),
                                            outputs);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

fmi2Real Fmi2Object::getOutput(size_t outNum) const
{
    fmi2Real out = -1e48;
    if (outNum < activeOutputs.getVRcount()) {
        commonFunctions->fmi2GetReal(comp, activeOutputs.getValueRef() + outNum, 1, &out);
    }
    return out;
}

FmiVariableSet Fmi2Object::getVariableSet(const std::string& variable) const
{
    return info->getVariableSet(variable);
}

FmiVariableSet Fmi2Object::getVariableSet(int index) const
{
    return info->getVariableSet(index);
}

std::vector<std::string> Fmi2Object::getOutputNames() const
{
    std::vector<std::string> oVec;
    if (activeOutputs.getVRcount() == 0) {
        oVec = info->getVariableNames("outputs");
    } else {
        oVec.reserve(activeOutputs.getVRcount());
        for (auto& os : activeOutputIndices) {
            oVec.push_back(info->getVariableInfo(os).name);
        }
    }

    return oVec;
}

std::vector<std::string> Fmi2Object::getInputNames() const
{
    std::vector<std::string> oVec;
    if (activeInputs.getVRcount() == 0) {
        oVec = info->getVariableNames("inputs");
    } else {
        oVec.reserve(activeInputs.getVRcount());
        for (auto& os : activeInputIndices) {
            oVec.push_back(info->getVariableInfo(os).name);
        }
    }
    return oVec;
}

bool Fmi2Object::isParameter(const std::string& param, FmiVariableType type)
{
    auto& vi = info->getVariableInfo(param);
    if (vi.index >= 0) {
        if ((vi.causality == FmiCausalityType::parameter) ||
            (vi.causality == FmiCausalityType::input)) {
            if (vi.type == type) {
                return true;
            }
            if (type == FmiVariableType::numeric) {
                if (vi.type != FmiVariableType::string) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool Fmi2Object::isVariable(const std::string& var, FmiVariableType type)
{
    auto& vi = info->getVariableInfo(var);
    if (vi.index >= 0) {
        if (vi.type == type) {
            return true;
        }
        if (type == FmiVariableType::numeric) {
            if (vi.type != FmiVariableType::string) {
                return true;
            }
        }
    }

    return false;
}

void Fmi2Object::handleNonOKReturnValues(fmi2Status retval) const
{
    switch (retval) {
        case fmi2Status::fmi2OK:
            return;
        case fmi2Status::fmi2Pending:
            return;
        case fmi2Status::fmi2Discard:
            if (exceptionOnDiscard) {
                throw(FmiDiscardException());
            }
            break;
        case fmi2Status::fmi2Warning:
            if (exceptionOnWarning) {
                throw(FmiWarningException());
            }
            break;
        case fmi2Status::fmi2Error:
            throw(FmiErrorException());
        case fmi2Status::fmi2Fatal:
            throw(FmiFatalException());
        default:
            throw(FmiException());
    }
}
