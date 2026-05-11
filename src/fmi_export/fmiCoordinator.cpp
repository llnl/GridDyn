/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiCoordinator.h"

#include "fmiCollector.h"
#include "fmiEvent.h"
#include "gmlc/containers/mapOps.hpp"
#include <algorithm>
#include <cstdio>
#include <print>
#include <string>
#include <utility>

namespace griddyn::fmi {
fmiCoordinator::fmiCoordinator(const std::string& /* unused */): coreObject("fmiCoordinator") {}

static auto searchFunc = [](const auto& vp1, const auto& vp2) { return (vp1.first < vp2.first); };

void fmiCoordinator::registerParameter(const std::string& paramName, fmiEvent* eventObject)
{
    auto found = mVrNames.find(paramName);
    if (found == mVrNames.end()) {
        auto valueReference = mNextVr++;
        mParamVr.emplace_back(valueReference, inputSet{.name = paramName, .event = eventObject});
        mVrNames.emplace(paramName, valueReference);
    } else {
        auto valueReference = found->second;
        auto insertLocation = std::lower_bound(mParamVr.begin(),
                                               mParamVr.end(),
                                               vrInputPair(valueReference, inputSet()),
                                               searchFunc);
        if ((insertLocation != mParamVr.end()) && (insertLocation->first == valueReference)) {
            while ((insertLocation != mParamVr.end()) &&
                   (insertLocation->first == valueReference)) {
                ++insertLocation;
            }
            mParamVr.emplace(insertLocation,
                             valueReference,
                             inputSet{.name = paramName, .event = eventObject});
        } else {
            valueReference = mNextVr++;
            mParamVr.emplace_back(valueReference,
                                  inputSet{.name = paramName, .event = eventObject});
            mVrNames.emplace(paramName, valueReference);
        }
    }
}

void fmiCoordinator::registerInput(const std::string& inputName, fmiEvent* eventObject)
{
    auto found = mVrNames.find(inputName);
    if (found == mVrNames.end()) {
        auto valueReference = mNextVr++;
        mInputVr.emplace_back(valueReference, inputSet{.name = inputName, .event = eventObject});
        mVrNames.emplace(inputName, valueReference);
    } else {
        auto valueReference = found->second;
        auto insertLocation = std::lower_bound(mInputVr.begin(),
                                               mInputVr.end(),
                                               vrInputPair(valueReference, inputSet()),
                                               searchFunc);
        while ((insertLocation != mParamVr.end()) && (insertLocation->first == valueReference)) {
            ++insertLocation;
        }
        if ((insertLocation != mParamVr.end()) && (insertLocation->first == valueReference)) {
            mInputVr.emplace(++insertLocation,
                             valueReference,
                             inputSet{.name = inputName, .event = eventObject});
        } else {
            valueReference = mNextVr++;
            mInputVr.emplace_back(valueReference,
                                  inputSet{.name = inputName, .event = eventObject});
            mVrNames.emplace(inputName, valueReference);
        }
    }
}

void fmiCoordinator::registerOutput(const std::string& outputName,
                                    int column,
                                    fmiCollector* outputCollector)
{
    auto valueReference = mNextVr++;
    mOutputVr.emplace_back(valueReference,
                           outputSet{.name = outputName,
                                     .column = column,
                                     .outputIndex = static_cast<index_t>(mOutputVr.size()),
                                     .collector = outputCollector});
    mVrNames.emplace(outputName, valueReference);
    mCollectors.push_back(outputCollector);
    auto collectorEnd = std::unique(mCollectors.begin(), mCollectors.end());
    mCollectors.erase(collectorEnd, mCollectors.end());
    mOutputPoints.push_back(0.0);
}

bool fmiCoordinator::sendInput(index_t valueReference, double inputValue)
{
    auto res = std::lower_bound(mInputVr.begin(),
                                mInputVr.end(),
                                vrInputPair(valueReference, inputSet()),
                                searchFunc);
    if ((res != mInputVr.end()) && (res->first == valueReference)) {
        while ((res != mInputVr.end()) && (res->first == valueReference)) {
            res->second.event->setValue(inputValue);
            res->second.event->trigger();
            ++res;
        }
        return true;
    }
    res = std::lower_bound(mParamVr.begin(),
                           mParamVr.end(),
                           vrInputPair(valueReference, inputSet()),
                           searchFunc);
    if ((res != mParamVr.end()) && (res->first == valueReference)) {
        while ((res != mParamVr.end()) && (res->first == valueReference)) {
            res->second.event->setValue(inputValue);
            res->second.event->trigger();
            ++res;
        }
        return true;
    }
    logging::warning(this, "invalid value reference {}", valueReference);
    return false;
}

index_t fmiCoordinator::findVR(const std::string& varName) const
{
    return mapFind(mVrNames, varName, kNullLocation);
}

bool fmiCoordinator::sendInput(index_t valueReference, const char* stringValue)
{
    auto res = std::lower_bound(mParamVr.begin(),
                                mParamVr.end(),
                                vrInputPair(valueReference, inputSet()),
                                searchFunc);
    if ((res != mParamVr.end()) && (res->first == valueReference) &&
        (res->second.event->mEventType == fmi::fmiEvent::fmiEventType::string_parameter)) {
        while ((res != mParamVr.end()) && (res->first == valueReference) &&
               (res->second.event->mEventType == fmi::fmiEvent::fmiEventType::string_parameter)) {
            std::println("updating string value {} to {}", res->second.name, stringValue);
            res->second.event->updateStringValue(stringValue);
            res->second.event->trigger();
            ++res;
        }
        return true;
    }
    logging::warning(this, "invalid value reference {}", valueReference);
    return false;
}

double fmiCoordinator::getOutput(index_t valueReference)
{
    auto res = std::lower_bound(mOutputVr.begin(),
                                mOutputVr.end(),
                                vrOutputPair(valueReference, outputSet()),
                                searchFunc);
    if ((res != mOutputVr.end()) && (res->first == valueReference)) {
        return mOutputPoints[res->second.outputIndex];
    }
    auto res2 = std::lower_bound(mParamVr.begin(),
                                 mParamVr.end(),
                                 vrInputPair(valueReference, inputSet()),
                                 searchFunc);
    if ((res2 != mParamVr.end()) && (res2->first == valueReference)) {
        return res2->second.event->query();
    }
    auto res3 = std::lower_bound(mInputVr.begin(),
                                 mInputVr.end(),
                                 vrInputPair(valueReference, inputSet()),
                                 searchFunc);
    if ((res3 != mInputVr.end()) && (res3->first == valueReference)) {
        return res3->second.event->query();
    }
    return kNullVal;
}

void fmiCoordinator::updateOutputs(coreTime time)
{
    for (auto* collector : mCollectors) {
        collector->trigger(time);
    }
    for (auto& output : mOutputVr) {
        mOutputPoints[output.second.outputIndex] =
            output.second.collector->getValue(output.second.column);
    }
}

const std::string& fmiCoordinator::getFMIName() const
{
    return getParent()->getName();
}

void fmiCoordinator::addHelper(std::shared_ptr<helperObject> helperObjectPtr)
{
    const std::scoped_lock helperLock(mHelperProtector);
    mHelpers.push_back(std::move(helperObjectPtr));
}

bool fmiCoordinator::isStringParameter(const vrInputPair& param)
{
    return (param.second.event->mEventType == fmi::fmiEvent::fmiEventType::string_parameter);
}

}  // namespace griddyn::fmi
