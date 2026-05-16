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

Fmi2ModelExchangeObject::Fmi2ModelExchangeObject(
    fmi2Component cmp,
    std::shared_ptr<const FmiInfo> keyInfo,
    std::shared_ptr<const FmiCommonFunctions> comFunc,
    std::shared_ptr<const FmiModelExchangeFunctions> meFunc):
    Fmi2Object(cmp, std::move(keyInfo), std::move(comFunc)),
    ModelExchangeFunctions(std::move(meFunc))
{
    numIndicators = info->getCounts("events");
    numStates = info->getCounts("states");
    if (numStates == 0) {
        hasTime = false;
    }
}

void Fmi2ModelExchangeObject::setMode(FmuMode mode)
{
    std::println("setting mode {}", static_cast<int>(mode));
    fmi2Status ret = fmi2Error;
    switch (currentMode) {
        case FmuMode::instantiatedMode:
        case FmuMode::initializationMode:

            if (mode == FmuMode::continuousTimeMode) {
                std::println(" entering event mode");
                Fmi2Object::setMode(FmuMode::eventMode);
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
                Fmi2Object::setMode(mode);
            }
            break;
        case FmuMode::continuousTimeMode:
            if (mode == FmuMode::eventMode) {
                ret = ModelExchangeFunctions->fmi2EnterEventMode(comp);
            }
            break;
        case FmuMode::eventMode:
            if (mode == FmuMode::eventMode) {
                ret = ModelExchangeFunctions->fmi2EnterEventMode(comp);
            } else if (mode == FmuMode::continuousTimeMode) {
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
            Fmi2Object::setMode(mode);
            return;
    }

    if (ret == fmi2OK) {
        currentMode = mode;
    } else if (currentMode != mode) {
        handleNonOKReturnValues(ret);
    }
}

void Fmi2ModelExchangeObject::newDiscreteStates(fmi2EventInfo* fmi2eventInfo)
{
    auto ret = ModelExchangeFunctions->fmi2NewDiscreteStates(comp, fmi2eventInfo);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2ModelExchangeObject::completedIntegratorStep(fmi2Boolean noSetFMUStatePriorToCurrentPoint,
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
void Fmi2ModelExchangeObject::setTime(fmi2Real time)
{
    if (hasTime) {
        auto ret = ModelExchangeFunctions->fmi2SetTime(comp, time);
        if (ret != fmi2Status::fmi2OK) {
            handleNonOKReturnValues(ret);
        }
    }
}
void Fmi2ModelExchangeObject::setStates(const fmi2Real states[])
{
    auto ret = ModelExchangeFunctions->fmi2SetContinuousStates(comp, states, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2ModelExchangeObject::getDerivatives(fmi2Real derivatives[]) const
{
    auto ret = ModelExchangeFunctions->fmi2GetDerivatives(comp, derivatives, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2ModelExchangeObject::getEventIndicators(fmi2Real eventIndicators[]) const
{
    auto ret = ModelExchangeFunctions->fmi2GetEventIndicators(comp, eventIndicators, numIndicators);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2ModelExchangeObject::getStates(fmi2Real states[]) const
{
    auto ret = ModelExchangeFunctions->fmi2GetContinuousStates(comp, states, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}
void Fmi2ModelExchangeObject::getNominalsOfContinuousStates(fmi2Real nominalValues[]) const
{
    auto ret =
        ModelExchangeFunctions->fmi2GetNominalsOfContinuousStates(comp, nominalValues, numStates);
    if (ret != fmi2Status::fmi2OK) {
        handleNonOKReturnValues(ret);
    }
}

std::vector<std::string> Fmi2ModelExchangeObject::getStateNames() const
{
    return info->getVariableNames("state");
}
