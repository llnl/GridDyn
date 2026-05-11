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

void fmiCoordinator::registerParameter(const std::string& paramName, fmiEvent* evnt)
{
    auto found = mVrNames.find(paramName);
    if (found == mVrNames.end()) {
        auto valueReference = mNextVr++;
        mParamVr.emplace_back(valueReference, inputSet{paramName, evnt});
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
            mParamVr.emplace(insertLocation, valueReference, inputSet{paramName, evnt});
        } else {
            valueReference = mNextVr++;
            mParamVr.emplace_back(valueReference, inputSet{paramName, evnt});
            mVrNames.emplace(paramName, valueReference);
        }
    }
}

void fmiCoordinator::registerInput(const std::string& inputName, fmiEvent* evnt)
{
    auto found = mVrNames.find(inputName);
    if (found == mVrNames.end()) {
        auto valueReference = mNextVr++;
        mInputVr.emplace_back(valueReference, inputSet{inputName, evnt});
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
            mInputVr.emplace(++insertLocation, valueReference, inputSet{inputName, evnt});
        } else {
            valueReference = mNextVr++;
            mInputVr.emplace_back(valueReference, inputSet{inputName, evnt});
            mVrNames.emplace(inputName, valueReference);
        }
    }
}

void fmiCoordinator::registerOutput(const std::string& outputName, int column, fmiCollector* out)
{
    auto valueReference = mNextVr++;
    mOutputVr.emplace_back(
        valueReference,
        outputSet{outputName, column, static_cast<index_t>(mOutputVr.size()), out});
    mVrNames.emplace(outputName, valueReference);
    mCollectors.push_back(out);
    auto collectorEnd = std::unique(mCollectors.begin(), mCollectors.end());
    mCollectors.erase(collectorEnd, mCollectors.end());
    mOutputPoints.push_back(0.0);
}

bool fmiCoordinator::sendInput(index_t vr, double val)
{
    auto res =
        std::lower_bound(mInputVr.begin(), mInputVr.end(), vrInputPair(vr, inputSet()), searchFunc);
    if ((res != mInputVr.end()) && (res->first == vr)) {
        while ((res != mInputVr.end()) && (res->first == vr)) {
            res->second.event->setValue(val);
            res->second.event->trigger();
            ++res;
        }
        return true;
    }
    res =
        std::lower_bound(mParamVr.begin(), mParamVr.end(), vrInputPair(vr, inputSet()), searchFunc);
    if ((res != mParamVr.end()) && (res->first == vr)) {
        while ((res != mParamVr.end()) && (res->first == vr)) {
            res->second.event->setValue(val);
            res->second.event->trigger();
            ++res;
        }
        return true;
    }
    logging::warning(this, "invalid value reference {}", vr);
    return false;
}

index_t fmiCoordinator::findVR(const std::string& varName) const
{
    return mapFind(mVrNames, varName, kNullLocation);
}

bool fmiCoordinator::sendInput(index_t vr, const char* s)
{
    auto res =
        std::lower_bound(mParamVr.begin(), mParamVr.end(), vrInputPair(vr, inputSet()), searchFunc);
    if ((res != mParamVr.end()) && (res->first == vr) &&
        (res->second.event->mEventType == fmi::fmiEvent::fmiEventType::string_parameter)) {
        while ((res != mParamVr.end()) && (res->first == vr) &&
               (res->second.event->mEventType == fmi::fmiEvent::fmiEventType::string_parameter)) {
            std::println("updating string value {} to {}", res->second.name.c_str(), s);
            res->second.event->updateStringValue(s);
            res->second.event->trigger();
            ++res;
        }
        return true;
    }
    logging::warning(this, "invalid value reference {}", vr);
    return false;
}

double fmiCoordinator::getOutput(index_t vr)
{
    auto res = std::lower_bound(mOutputVr.begin(),
                                mOutputVr.end(),
                                vrOutputPair(vr, outputSet()),
                                searchFunc);
    if ((res != mOutputVr.end()) && (res->first == vr)) {
        return mOutputPoints[res->second.outputIndex];
    }
    auto res2 =
        std::lower_bound(mParamVr.begin(), mParamVr.end(), vrInputPair(vr, inputSet()), searchFunc);
    if ((res2 != mParamVr.end()) && (res2->first == vr)) {
        return res2->second.event->query();
    }
    auto res3 =
        std::lower_bound(mInputVr.begin(), mInputVr.end(), vrInputPair(vr, inputSet()), searchFunc);
    if ((res3 != mInputVr.end()) && (res3->first == vr)) {
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

void fmiCoordinator::addHelper(std::shared_ptr<helperObject> ho)
{
    std::lock_guard<std::mutex> helperLock(mHelperProtector);
    mHelpers.push_back(std::move(ho));
}

bool fmiCoordinator::isStringParameter(const vrInputPair& param)
{
    return (param.second.event->mEventType == fmi::fmiEvent::fmiEventType::string_parameter);
}

}  // namespace griddyn::fmi
