/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Collector.h"

#include "../events/Event.h"
#include "GridGrabbers.h"
#include "Recorder.h"
#include "StateGrabber.h"
#include "core/CoreExceptions.h"
#include "core/FactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::fsize_t;

static ClassFactory<Collector> gCollFac("collector");

static ChildClassFactory<Recorder, Collector>
    gRFac(std::vector<std::string>{"recorder", "rec", "file"}, "recorder");

namespace {
    CoreTime checkedPeriod(double seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0.0 || seconds >= static_cast<double>(maxTime)) {
            throw InvalidParameterValue(
                "collector interval must be finite and within the clock range");
        }
        const CoreTime interval(seconds);
        if (interval <= timeZero || interval >= maxTime) {
            throw InvalidParameterValue(
                "collector interval is outside the clock resolution or range");
        }
        return interval;
    }
}  // namespace

Collector::Collector(CoreTime time0, CoreTime period):
    mTimePeriod(period), mRequestedPeriod(period), mTriggerTime(time0), mInitialTriggerTime(time0)
{
    // FMI and other externally driven collectors use this pair to disable scheduling.
    if (time0 != maxTime || period != maxTime) {
        static_cast<void>(checkedPeriod(period));
    }
}

Collector::Collector(const std::string& collectorName):
    HelperObject(collectorName), mTimePeriod(1.0), mRequestedPeriod(1.0), mTriggerTime(timeZero)
{
}

std::unique_ptr<Collector> Collector::clone() const
{
    auto col = std::make_unique<Collector>();
    cloneTo(col.get());
    return col;
}

void Collector::cloneTo(Collector* col) const
{
    if (col == this) {
        return;
    }
    col->mRequestedPeriod = mRequestedPeriod;
    col->mTimePeriod = mTimePeriod;
    col->setName(getName());
    col->setDescription(getDescription());
    col->mStartTime = mStartTime;
    col->mStopTime = mStopTime;
    col->mTriggerTime = mTriggerTime;
    col->mInitialTriggerTime = mInitialTriggerTime;
    col->mLastTriggerTime = mLastTriggerTime;
    col->mArmed = mArmed;
    col->mDelayProcess = mDelayProcess;
    col->mVectorName = mVectorName;
    col->mColumns = mColumns;
    col->mRecheck = mRecheck;
    col->clearWarnings();
    col->mPoints.clear();
    for (const auto& point : mPoints) {
        col->mPoints.emplace_back(point.mDataGrabber ? point.mDataGrabber->clone() : nullptr,
                                  point.mStateGrabber ? point.mStateGrabber->clone() : nullptr,
                                  point.mRequestedColumn,
                                  point.mColumnCount,
                                  point.mColumnName);
        col->mPoints.back().mColumn = point.mColumn;
    }
    col->mData.assign(mData.size(), 0.0);
}

void Collector::updateObject(CoreObject* gco, ObjectUpdateMode mode)
{
    for (const auto& dataPoint : mPoints) {
        if (dataPoint.mDataGrabber) {
            dataPoint.mDataGrabber->updateObject(gco, mode);
            if (dataPoint.mDataGrabber->vectorGrab) {
                mRecheck = true;
            }
        }
        if (dataPoint.mStateGrabber) {
            dataPoint.mStateGrabber->updateObject(gco, mode);
        }
    }
}

CoreObject* Collector::getObject() const
{
    if (!mPoints.empty()) {
        if (mPoints[0].mDataGrabber) {
            return mPoints.front().mDataGrabber->getObject();
        }
        if (mPoints[0].mStateGrabber) {
            return mPoints.front().mStateGrabber->getObject();
        }
    }
    return nullptr;
}

void Collector::getObjects(std::vector<CoreObject*>& objects) const
{
    for (const auto& dataPoint : mPoints) {
        if (dataPoint.mDataGrabber) {
            dataPoint.mDataGrabber->getObjects(objects);
        } else if (dataPoint.mStateGrabber) {
            dataPoint.mStateGrabber->getObjects(objects);
        }
    }
}

std::vector<std::string> Collector::getColumnDescriptions() const
{
    stringVec res;
    size_t nextColumn = 0;
    for (const auto& datapoint : mPoints) {
        const auto column = datapoint.mRequestedColumn >= 0 ?
            static_cast<size_t>(datapoint.mRequestedColumn) :
            nextColumn;
        stringVec descriptions;
        if ((datapoint.mDataGrabber && datapoint.mDataGrabber->vectorGrab) ||
            (!datapoint.mDataGrabber && datapoint.mStateGrabber->vectorGrab)) {
            if (datapoint.mDataGrabber) {
                datapoint.mDataGrabber->getDesc(descriptions);
            } else {
                datapoint.mStateGrabber->getDesc(descriptions);
            }
            if (mVectorName) {
                auto name = datapoint.mColumnName;
                if (name.empty()) {
                    name = datapoint.mDataGrabber ? datapoint.mDataGrabber->getDesc() :
                                                    datapoint.mStateGrabber->field;
                }
                for (size_t index = 0; index < descriptions.size(); ++index) {
                    descriptions[index] = name + '[' + std::to_string(index) + ']';
                }
            }
        } else {
            if (!datapoint.mColumnName.empty()) {
                descriptions.push_back(datapoint.mColumnName);
            } else if (datapoint.mDataGrabber) {
                descriptions.push_back(datapoint.mDataGrabber->getDesc());
            } else {
                descriptions.push_back(datapoint.mStateGrabber->field);
            }
        }
        nextColumn = std::max(nextColumn, column + descriptions.size());
        res.resize(nextColumn);
        std::copy(descriptions.begin(), descriptions.end(), res.begin() + column);
    }
    return res;
}

void Collector::set(std::string_view param, double val)
{
    if (param == "period") {
        mRequestedPeriod = checkedPeriod(val);
        mTimePeriod = mRequestedPeriod;
    } else if (param == "frequency") {
        if (!std::isfinite(val) || val <= 0.0 || !std::isfinite(1.0 / val)) {
            throw InvalidParameterValue("collector frequency must be finite and positive");
        }
        mRequestedPeriod = checkedPeriod(1.0 / val);
        mTimePeriod = mRequestedPeriod;
    } else if ((param == "triggertime") || (param == "trigger") || (param == "time")) {
        mTriggerTime = val;
        mInitialTriggerTime = mTriggerTime;
    } else if ((param == "starttime") || (param == "start")) {
        mStartTime = val;
        mTriggerTime = mStartTime;
        mInitialTriggerTime = mTriggerTime;
    } else if ((param == "stoptime") || (param == "stop")) {
        mStopTime = val;
    } else if (param == "period_resolution") {
        const double resolution = checkedPeriod(val);
        const auto periods =
            std::max(1.0, std::round(static_cast<double>(mRequestedPeriod) / resolution));
        mTimePeriod = checkedPeriod(resolution * periods);
    } else {
        HelperObject::set(param, val);
    }
}

void Collector::set(std::string_view param, std::string_view val)
{
    if (!param.empty() && param.front() == '#') {
    } else {
        HelperObject::set(param, val);
    }
}

void Collector::setFlag(std::string_view flag, bool val)
{
    if (flag == "vector_name") {
        mVectorName = val;
    } else {
        HelperObject::setFlag(flag, val);
    }
}

void Collector::setTime(CoreTime time)
{
    if (time > mTriggerTime) {
        mTriggerTime = time;
    }
}

void Collector::recheckColumns()
{
    int columnTracker = 0;
    auto& vals = mVectorBuffer;
    for (auto& dataPoint : mPoints) {
        dataPoint.mColumn =
            dataPoint.mRequestedColumn >= 0 ? dataPoint.mRequestedColumn : columnTracker;
        if ((dataPoint.mDataGrabber && dataPoint.mDataGrabber->vectorGrab) ||
            (!dataPoint.mDataGrabber && dataPoint.mStateGrabber->vectorGrab)) {
            if (dataPoint.mDataGrabber) {
                dataPoint.mDataGrabber->grabVectorData(vals);
            } else {
                dataPoint.mStateGrabber->grabVectorData(emptyStateData, cLocalSolverMode, vals);
            }
            dataPoint.mColumnCount = static_cast<int>(vals.size());
        } else {
            dataPoint.mColumnCount = 1;
        }
        columnTracker = std::max(columnTracker, dataPoint.mColumn + dataPoint.mColumnCount);
    }
    mColumns = static_cast<count_t>(columnTracker);
    mData.resize(columnTracker);
    mRecheck = false;
}

count_t Collector::grabData(double* outputData, index_t outputCount)
{
    auto& vals = mVectorBuffer;
    count_t currentCount = 0;
    if (outputData == nullptr || outputCount <= 0) {
        return 0;
    }
    const auto outputLimit = static_cast<count_t>(outputCount);
    if (mRecheck) {
        recheckColumns();
    }
    for (auto& datapoint : mPoints) {
        const auto column = datapoint.mColumn;
        if (column < 0 || column >= outputCount) {
            continue;
        }
        if ((datapoint.mDataGrabber && datapoint.mDataGrabber->vectorGrab) ||
            (!datapoint.mDataGrabber && datapoint.mStateGrabber->vectorGrab)) {
            if (datapoint.mDataGrabber) {
                datapoint.mDataGrabber->grabVectorData(vals);
            } else {
                datapoint.mStateGrabber->grabVectorData(emptyStateData, cLocalSolverMode, vals);
            }
            if (static_cast<int>(vals.size()) != datapoint.mColumnCount) {
                throw InvalidParameterValue(
                    "collector vector size changed; recheck columns before sampling");
            }
            const auto remaining = outputCount - column;
            const auto valueCount = static_cast<index_t>(vals.size());
            const auto copyCount = std::min(remaining, valueCount);
            if (copyCount == 0) {
                continue;
            }
            std::copy_n(vals.begin(), copyCount, outputData + column);
            const auto nextCount = (copyCount >= remaining) ?
                outputLimit :
                static_cast<count_t>(column) + static_cast<count_t>(copyCount);
            currentCount = std::max(currentCount, nextCount);
        } else {
            outputData[column] = datapoint.mDataGrabber ?
                datapoint.mDataGrabber->grabData() :
                datapoint.mStateGrabber->grabData(emptyStateData, cLocalSolverMode);
            const auto nextCount =
                (column >= (outputCount - 1)) ? outputLimit : static_cast<count_t>(column) + 1;
            currentCount = std::max(currentCount, nextCount);
        }
    }
    currentCount = std::min(currentCount, outputLimit);
    return currentCount;
}

ChangeCode Collector::trigger(CoreTime time)
{
    if (mRecheck) {
        recheckColumns();
    }
    grabData(mData.data(), static_cast<index_t>(mData.size()));
    mLastTriggerTime = time;
    if (time >= mTriggerTime) {
        mTriggerTime += (std::floor((time - mTriggerTime) / mTimePeriod) + 1.0) * mTimePeriod;
        if (mTriggerTime <= time) {
            mTriggerTime = time + mTimePeriod;
        }
    }
    if (mTriggerTime > mStopTime) {
        mTriggerTime = maxTime;
    }
    return ChangeCode::NO_CHANGE;
}

int Collector::getColumn(int requestedColumn) const
{
    int retColumn = requestedColumn;
    if (requestedColumn < 0) {
        if (mRecheck) {
            retColumn = -1;
        } else {
            retColumn = static_cast<int>(mColumns);
        }
    }
    return retColumn;
}

void Collector::updateColumns(int requestedColumn)
{
    if (requestedColumn >= static_cast<int>(mColumns)) {
        mColumns = requestedColumn + 1;
    }

    if (!mRecheck) {
        mData.resize(mColumns);
    }
}

void Collector::add(std::shared_ptr<GridGrabber> ggb, int requestedColumn)  // NOLINT
{
    add(std::move(ggb), nullptr, requestedColumn);
}

void Collector::add(std::shared_ptr<StateGrabber> sst, int requestedColumn)  // NOLINT
{
    add(nullptr, std::move(sst), requestedColumn);
}

// NOLINTNEXTLINE
void Collector::add(std::shared_ptr<GridGrabber> ggb,
                    std::shared_ptr<StateGrabber> sst,  // NOLINT
                    int requestedColumn)
{
    if (!ggb && !sst) {
        throw AddFailureException();
    }
    const bool needsRecheck = mRecheck || (ggb ? ggb->vectorGrab : sst->vectorGrab);
    mPoints.emplace_back(ggb, sst, requestedColumn);
    mRecheck = true;
    if (ggb && ggb->customDesc) {
        mPoints.back().mColumnName = ggb->getDesc();
    }
    recheckColumns();
    mRecheck = needsRecheck;
    dataPointAdded(mPoints.back());
    if ((!ggb || !ggb->loaded) && (!sst || !sst->loaded)) {
        addWarning("grabber not loaded");
    }
}

// a notification that something was added much more useful in derived classes
void Collector::dataPointAdded(const CollectorPoint& /*cp*/) {}
// NOLINTNEXTLINE(misc-no-recursion)
void Collector::add(const GridGrabberInfo& gdRI, CoreObject* obj)
{
    if (gdRI.field.empty())  // any field specification overrides the offset
    {
        if (gdRI.offset >= 0) {
            auto ggb = createGrabber(gdRI.offset, obj);
            // auto sst = makeStateGrabbers(gdRI.offset, obj);
            if (ggb) {
                ggb->bias = gdRI.bias;
                ggb->gain = gdRI.gain;
                add(std::shared_ptr<GridGrabber>(std::move(ggb)), gdRI.column);
                return;
            }
            throw(AddFailureException());
        }
        if (obj != nullptr) {
            obj->log(obj,
                     PrintLevel::WARNING,
                     "unable to create Collector no field or offset specified");
        }
        addWarning("unable to create Collector no field or offset specified");
        return;
    }

    auto fldGrabbers = makeGrabbers(gdRI.field, obj);
    int requestedColumn = gdRI.column;
    for (auto& grabber : fldGrabbers) {
        grabber->gain *= gdRI.gain;
        grabber->bias = std::fma(grabber->bias, gdRI.gain, gdRI.bias);
        if (gdRI.outputUnits != units::defunit) {
            grabber->outputUnits = gdRI.outputUnits;
        }
        add(std::shared_ptr<GridGrabber>(std::move(grabber)), requestedColumn);
        requestedColumn = -1;
    }
    if (fldGrabbers.empty()) {
        if (obj != nullptr) {
            obj->log(obj, PrintLevel::WARNING, "no grabbers created from " + gdRI.field);
        }
        addWarning("no grabbers created from " + gdRI.field);
        throw(AddFailureException());
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void Collector::add(std::string_view field, CoreObject* obj)
{
    GridGrabberInfo info;
    info.field = field;
    add(info, obj);
}

void Collector::reset()
{
    mPoints.clear();
    mData.clear();
    mWarnList.clear();
    mWarningCount = 0;
    mColumns = 0;
    mRecheck = false;
    resetSchedule();
}

void Collector::resetSchedule()
{
    mLastTriggerTime = negTime;
    mTriggerTime = mTimePeriod == maxTime ? maxTime : mInitialTriggerTime;
}

void Collector::flush() {}
const std::string& Collector::getSinkName() const
{
    static const std::string emptyString;
    return emptyString;
}
std::unique_ptr<Collector> makeCollector(std::string_view type, const std::string& name)
{
    if (name.empty()) {
        return CoreClassFactory<Collector>::instance()->createObject(type);
    }
    return CoreClassFactory<Collector>::instance()->createObject(type, name);
}

}  // namespace griddyn
