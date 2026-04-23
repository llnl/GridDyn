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

fmi2ModelExchangeObject::fmi2ModelExchangeObject(
    fmi2Component cmp,
    std::shared_ptr<const fmiInfo> keyInfo,
    std::shared_ptr<const fmiCommonFunctions> comFunc,
    std::shared_ptr<const fmiModelExchangeFunctions> meFunc):
    fmi2Object(cmp, std::move(keyInfo), std::move(comFunc)),
    ModelExchangeFunctions(std::move(meFunc))
{
    numIndicators = info->getCounts("events");
    numStates = info->getCounts("states");
    if (numStates == 0) {
        hasTime = false;
    }
}

void fmi2ModelExchangeObject::setMode(fmuMode mode)
{
    std::println("setting mode {}", static_cast<int>(mode));
    fmi2Status ret = fmi2Error;
    switch (currentMode) {
        case fmuMode::instantiatedMode:
        case fmuMode::initializationMode:

            if (mode == fmuMode::continuousTimeMode) {
                std::println(" entering event mode");
                fmi2Object::setMode(fmuMode::eventMode);
                std::println(" now in event event mode");
                if (numStates > 0) {
                    std::println("now entering continuous time mode");
                    ret = ModelExchangeFunctions->fmi2EnterContinuousTimeMode(comp);
                    std::println("entered continuous time mode return code {}",
                                 static_cast<int>(ret));
                } else {
                    ret = fmi2OK;
                }
            } else {
                fmi2Object::setMode(mode);
            }
            break;
        case fmuMode::continuousTimeMode:
            if (mode == fmuMode::eventMode) {
                ret = ModelExchangeFunctions->fmi2EnterEventMode(comp);
            }
            break;
        case fmuMode::eventMode:
            if (mode == fmuMode::eventMode) {
                ret = ModelExchangeFunctions->fmi2EnterEventMode(comp);
            } else if (mode == fmuMode::continuousTimeMode) {
                if (numStates > 0) {
                    std::println("now entering continuous time mode");
                    ret = ModelExchangeFunctions->fmi2EnterContinuousTimeMode(comp);
                    std::println("entered continuous time mode return code {}",
                                 static_cast<int>(ret));
                } else {
                    ret = fmi2OK;
                }
            }
            break;
        default:
            fmi2Object::setMode(mode);
            return;
    }

    if (ret == fmi2OK) {
        currentMode = mode;
    } else if (currentMode != mode) {
        handleNonOKReturnValues(ret);
    }
}

void fmi2ModelExchangeObject::newDiscreteStates(fmi2EventInfo* fmi2eventInfo)
{
    auto ret = ModelExchangeFunctions->fmi2NewDiscreteStates(comp, fmi2eventInfo);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void fmi2ModelExchangeObject::completedIntegratorStep(fmi2Boolean noSetFMUStatePriorToCurrentPoint,
                                                      fmi2Boolean* enterEventMode,
                                                      fmi2Boolean* terminatesSimulation)
{
    auto ret = ModelExchangeFunctions->fmi2CompletedIntegratorStep(comp,
                                                                   noSetFMUStatePriorToCurrentPoint,
                                                                   enterEventMode,
                                                                   terminatesSimulation);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void fmi2ModelExchangeObject::setTime(fmi2Real time)
{
    if (hasTime) {
        auto ret = ModelExchangeFunctions->fmi2SetTime(comp, time);
        if (ret != fmi2Status::fmi2OK) {
            handleNonOKReturnValues(ret);
        }
    }
}
void fmi2ModelExchangeObject::setStates(const fmi2Real states[])
{
    auto ret = ModelExchangeFunctions->fmi2SetContinuousStates(comp, states, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void fmi2ModelExchangeObject::getDerivatives(fmi2Real derivatives[]) const
{
    auto ret = ModelExchangeFunctions->fmi2GetDerivatives(comp, derivatives, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void fmi2ModelExchangeObject::getEventIndicators(fmi2Real eventIndicators[]) const
{
    auto ret = ModelExchangeFunctions->fmi2GetEventIndicators(comp, eventIndicators, numIndicators);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void fmi2ModelExchangeObject::getStates(fmi2Real states[]) const
{
    auto ret = ModelExchangeFunctions->fmi2GetContinuousStates(comp, states, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void fmi2ModelExchangeObject::getNominalsOfContinuousStates(fmi2Real nominalValues[]) const
{
    auto ret =
        ModelExchangeFunctions->fmi2GetNominalsOfContinuousStates(comp, nominalValues, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

std::vector<std::string> fmi2ModelExchangeObject::getStateNames() const
{
    return info->getVariableNames("state");
}
