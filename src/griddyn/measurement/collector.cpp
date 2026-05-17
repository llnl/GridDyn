/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "collector.h"

#include "../events/Event.h"
#include "Recorder.h"
#include "core/coreExceptions.h"
#include "core/factoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/TimeSeries.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gridGrabbers.h"
#include "stateGrabber.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::fsize_t;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static classFactory<collector> collFac("collector");

static childClassFactory<Recorder, collector>
    grFac(  // NOLINT(bugprone-throwing-static-initialization)
        std::vector<std::string>{"recorder", "rec", "file"},
        "recorder");

collector::collector(coreTime time0, coreTime period):
    mTimePeriod(period), mRequestedPeriod(period), mTriggerTime(time0)
{
}

collector::collector(const std::string& collectorName):
    helperObject(collectorName), mTimePeriod(1.0), mRequestedPeriod(1.0), mTriggerTime(timeZero)
{
}

std::unique_ptr<collector> collector::clone() const
{
    auto col = std::make_unique<collector>();
    cloneTo(col.get());
    return col;
}

void collector::cloneTo(collector* col) const
{
    col->mRequestedPeriod = mRequestedPeriod;
    col->mTimePeriod = mTimePeriod;
    col->setName(getName());
    col->mStartTime = mStartTime;
    col->mStopTime = mStopTime;
    col->mTriggerTime = mTriggerTime;
    col->mLastTriggerTime = mLastTriggerTime;
    for (size_t kk = 0; kk < mPoints.size(); ++kk) {
        if (kk >= col->mPoints.size()) {
            auto ggb = (mPoints[kk].mDataGrabber) ? mPoints[kk].mDataGrabber->clone() : nullptr;
            auto ggbst = (mPoints[kk].mStateGrabber) ? mPoints[kk].mStateGrabber->clone() : nullptr;
            col->mPoints.emplace_back(std::move(ggb), std::move(ggbst), mPoints[kk].mColumn);
        } else {
            if (col->mPoints[kk].mDataGrabber) {
                if (mPoints[kk].mDataGrabber) {
                    mPoints[kk].mDataGrabber->cloneTo(col->mPoints[kk].mDataGrabber.get());
                }
            } else if (mPoints[kk].mDataGrabber) {
                col->mPoints[kk].mDataGrabber = mPoints[kk].mDataGrabber->clone();
            }

            if (col->mPoints[kk].mStateGrabber) {
                if (mPoints[kk].mStateGrabber) {
                    mPoints[kk].mStateGrabber->cloneTo(col->mPoints[kk].mStateGrabber.get());
                }
            } else if (mPoints[kk].mStateGrabber) {
                col->mPoints[kk].mStateGrabber = mPoints[kk].mStateGrabber->clone();
            }

            col->mPoints[kk].mColumn = mPoints[kk].mColumn;
        }
    }
    col->mData.resize(mData.size());
}

void collector::updateObject(CoreObject* gco, object_update_mode mode)
{
    for (const auto& dataPoint : mPoints) {
        if (dataPoint.mDataGrabber) {
            dataPoint.mDataGrabber->updateObject(gco, mode);
            if (dataPoint.mDataGrabber->vectorGrab) {
                mRecheck = true;
            }
        } else if (dataPoint.mStateGrabber) {
            dataPoint.mStateGrabber->updateObject(gco, mode);
        }
    }
}

CoreObject* collector::getObject() const
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

void collector::getObjects(std::vector<CoreObject*>& objects) const
{
    for (const auto& dataPoint : mPoints) {
        if (dataPoint.mDataGrabber) {
            dataPoint.mDataGrabber->getObjects(objects);
        } else if (dataPoint.mStateGrabber) {
            dataPoint.mStateGrabber->getObjects(objects);
        }
    }
}

std::vector<std::string> collector::getColumnDescriptions() const
{
    stringVec res;
    res.resize(mData.size());
    for (const auto& datapoint : mPoints) {
        if (datapoint.mDataGrabber->vectorGrab) {
            stringVec vdesc;
            datapoint.mDataGrabber->getDesc(vdesc);
            for (size_t kk = 0; kk < vdesc.size(); ++kk) {
                if ((datapoint.mColumnName.empty()) || (!mVectorName)) {
                    res[datapoint.mColumn + kk] = vdesc[kk];
                } else {
                    res[datapoint.mColumn + kk] =
                        datapoint.mColumnName + "[" + std::to_string(kk) + "]";
                }
            }
        } else {
            if (datapoint.mColumnName.empty()) {
                res[datapoint.mColumn] = datapoint.mDataGrabber->getDesc();
            } else {
                res[datapoint.mColumn] = datapoint.mColumnName;
            }
        }
    }
    return res;
}

void collector::set(std::string_view param, double val)
{
    if (param == "period") {
        mRequestedPeriod = val;
        mTimePeriod = val;
    } else if (param == "frequency") {
        mRequestedPeriod = 1.0 / val;
        mTimePeriod = val;
    } else if ((param == "triggertime") || (param == "trigger") || (param == "time")) {
        mTriggerTime = val;
    } else if ((param == "starttime") || (param == "start")) {
        mStartTime = val;
        mTriggerTime = mStartTime;
    } else if ((param == "stoptime") || (param == "stop")) {
        mStopTime = val;
    } else if (param == "period_resolution") {
        if (val > 0) {
            auto per = static_cast<int>(std::round(mRequestedPeriod / val));
            mTimePeriod = (per == 0) ? val : val * per;
        }
    } else {
        helperObject::set(param, val);
    }
}

void collector::set(std::string_view param, std::string_view val)
{
    if (param.front() == '#') {
    } else {
        helperObject::set(param, val);
    }
}

void collector::setFlag(std::string_view flag, bool val)
{
    if (flag == "vector_name") {
        mVectorName = val;
    } else {
        helperObject::setFlag(flag, val);
    }
}

void collector::setTime(coreTime time)
{
    if (time > mTriggerTime) {
        mTriggerTime = time;
    }
}

void collector::recheckColumns()
{
    fsize_t columnTracker = 0;
    std::vector<double> vals;
    // for (size_t kk = 0; kk < mPoints.size(); ++kk)
    for (auto& dataPoint : mPoints) {
        if (dataPoint.mColumn == -1) {
            dataPoint.mColumn = columnTracker;
        }

        if (dataPoint.mDataGrabber->vectorGrab) {
            dataPoint.mDataGrabber->grabVectorData(vals);
            columnTracker += static_cast<fsize_t>(vals.size());
        } else {
            ++columnTracker;
        }
    }
    mData.resize(columnTracker);
    mRecheck = false;
}

count_t collector::grabData(double* outputData, index_t outputCount)
{
    std::vector<double> vals;
    count_t currentCount = 0;
    if (outputCount <= 0) {
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
        if (datapoint.mDataGrabber->vectorGrab) {
            datapoint.mDataGrabber->grabVectorData(vals);
            const auto remaining = outputCount - column;
            const auto valueCount = static_cast<index_t>(vals.size());
            const auto copyCount = std::min(remaining, valueCount);
            std::copy_n(vals.begin(), copyCount, outputData + column);
            const auto nextCount = static_cast<count_t>(column + copyCount);
            currentCount = std::max(currentCount, nextCount);
        } else {
            outputData[column] = datapoint.mDataGrabber->grabData();
            const auto nextCount = static_cast<count_t>(column + 1);
            currentCount = std::max(currentCount, nextCount);
        }
    }
    currentCount = std::min(currentCount, outputLimit);
    return currentCount;
}

change_code collector::trigger(coreTime time)
{
    std::vector<double> vals;

    if (mRecheck) {
        recheckColumns();
    }
    for (auto& datapoint : mPoints) {
        if (datapoint.mDataGrabber->vectorGrab) {
            datapoint.mDataGrabber->grabVectorData(vals);
            std::copy(vals.begin(), vals.end(), mData.begin() + datapoint.mColumn);
        } else {
            mData[datapoint.mColumn] = datapoint.mDataGrabber->grabData();
        }
    }
    mLastTriggerTime = time;
    int cnt = 0;
    while (time >= mTriggerTime) {
        mTriggerTime += mTimePeriod;
        ++cnt;
        if (cnt > 5) {
            mTriggerTime = time + mTimePeriod;
        }
    }
    if (mTriggerTime > mStopTime) {
        mTriggerTime = maxTime;
    }
    return change_code::no_change;
}

int collector::getColumn(int requestedColumn) const
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

void collector::updateColumns(int requestedColumn)
{
    if (requestedColumn >= static_cast<int>(mColumns)) {
        mColumns = requestedColumn + 1;
    }

    if (!mRecheck) {
        mData.resize(mColumns);
    }
}

// TODO(phlpt): Merge the repeated add-path code.
void collector::add(std::shared_ptr<gridGrabber> ggb, int requestedColumn)  // NOLINT
{
    const int newColumn = getColumn(requestedColumn);

    if (ggb->vectorGrab) {
        mRecheck = true;
    }

    updateColumns(newColumn);
    mPoints.emplace_back(ggb, nullptr, newColumn);
    if (!ggb->getDesc().empty()) {
        mPoints.back().mColumnName = ggb->getDesc();
    }
    dataPointAdded(mPoints.back());
    if (!ggb->loaded) {
        if (ggb->getObject() != nullptr) {
            addWarning("grabber not loaded invalid field:" + ggb->field);
        } else {
            addWarning("grabber object not valid");
        }
    }
}

void collector::add(std::shared_ptr<stateGrabber> sst, int requestedColumn)  // NOLINT
{
    const int newColumn = getColumn(requestedColumn);
    updateColumns(newColumn);

    mPoints.emplace_back(nullptr, sst, newColumn);

    dataPointAdded(mPoints.back());
    if (!sst->loaded) {
        if (sst->getObject() != nullptr) {
            addWarning("grabber not loaded invalid field:" + sst->field);
        } else {
            addWarning("grabber object not valid");
        }
    }
}

// NOLINTNEXTLINE
void collector::add(std::shared_ptr<gridGrabber> ggb,
                    std::shared_ptr<stateGrabber> sst,  // NOLINT
                    int requestedColumn)
{
    const int newColumn = getColumn(requestedColumn);
    updateColumns(newColumn);

    mPoints.emplace_back(ggb, sst, newColumn);
    if (!ggb->getDesc().empty()) {
        mPoints.back().mColumnName = ggb->getDesc();
    }
    dataPointAdded(mPoints.back());
    if ((!ggb->loaded) && (!sst->loaded)) {
        addWarning("grabber not loaded");
    }
}

// a notification that something was added much more useful in derived classes
void collector::dataPointAdded(const collectorPoint& /*cp*/) {}
// NOLINTNEXTLINE(misc-no-recursion)
void collector::add(const gridGrabberInfo& gdRI, CoreObject* obj)
{
    if (gdRI.field.empty())  // any field specification overrides the offset
    {
        if (gdRI.offset > 0) {
            auto ggb = createGrabber(gdRI.offset, obj);
            // auto sst = makeStateGrabbers(gdRI.offset, obj);
            if (ggb) {
                ggb->bias = gdRI.bias;
                ggb->gain = gdRI.gain;
                add(std::shared_ptr<gridGrabber>(std::move(ggb)), gdRI.column);
                return;
            }
            throw(addFailureException());
        }
        obj->log(obj,
                 print_level::warning,
                 "unable to create collector no field or offset specified");
        addWarning("unable to create collector no field or offset specified");
        return;
    }

    if (gdRI.field.find_first_of(",;") !=
        std::string::npos) {  // now go into a loop of the comma variables
        // if multiple fields were specified by comma or semicolon separation
        auto splitFields = gmlc::utilities::stringOps::splitlineBracket(gdRI.field, ",;");
        if (splitFields.size() > 1) {
            int ccol = gdRI.column;
            auto tempInfo = gdRI;
            for (const auto& fld : splitFields) {
                tempInfo.field = fld;
                if (ccol >= 0) {
                    /* this wouldn't work if the data was a vector grab, but in that case the
                     * recheck flag would be activated and this information overridden*/
                    tempInfo.column = ccol++;  // post increment intended
                }
                add(tempInfo, obj);
            }
            return;
        }
    }

    // now we get to the interesting bit
    auto fldGrabbers = makeGrabbers(gdRI.field, obj);
    auto stGrabbers = makeStateGrabbers(gdRI.field, obj);
    if (fldGrabbers.size() == 1) {
        // merge the gain and bias
        fldGrabbers[0]->gain *= gdRI.gain;
        fldGrabbers[0]->bias *= gdRI.gain;
        fldGrabbers[0]->bias += gdRI.bias;
        if (gdRI.outputUnits != units::defunit) {
            fldGrabbers[0]->outputUnits = gdRI.outputUnits;
        }
        // TODO(PT) incorporate state grabbers properly
        add(std::shared_ptr<gridGrabber>(std::move(fldGrabbers[0])), gdRI.column);
    } else {
        int ccol = gdRI.column;
        for (auto& ggb : fldGrabbers) {
            if (ccol > 0) {
                add(std::shared_ptr<gridGrabber>(std::move(ggb)), ccol++);
            } else {
                add(std::shared_ptr<gridGrabber>(std::move(ggb)));
            }
        }
    }
    if (fldGrabbers.empty()) {
        obj->log(obj, print_level::warning, "no grabbers created from " + gdRI.field);
        addWarning("no grabbers created from " + gdRI.field);
        throw(addFailureException());
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void collector::add(std::string_view field, CoreObject* obj)
{
    if (field.find_first_of(",;") !=
        std::string::npos) {  // now go into a loop of the comma variables
        // if multiple fields were delimited by comma or semicolon
        auto grabberStrings = gmlc::utilities::stringOps::splitlineBracket(field, ",;");
        for (const auto& fld : grabberStrings) {
            add(fld, obj);
        }
    } else {  // now we get to the interesting bit
        auto fldGrabbers = makeGrabbers(std::string{field}, obj);
        for (auto& ggb : fldGrabbers) {
            add(std::shared_ptr<gridGrabber>(std::move(ggb)));
        }
        if (fldGrabbers.empty()) {
            obj->log(obj,
                     print_level::warning,
                     std::string{"no grabbers created from "} + std::string{field});
            addWarning(std::string{"no grabbers created from "} + std::string{field});
            throw(addFailureException());
        }
    }
}

void collector::reset()
{
    mPoints.clear();
    mData.clear();
    mWarnList.clear();
    mWarningCount = 0;
    mTriggerTime = maxTime;
}

void collector::flush() {}
const std::string& collector::getSinkName() const
{
    static const std::string emptyString;
    return emptyString;
}
std::unique_ptr<collector> makeCollector(std::string_view type, const std::string& name)
{
    if (name.empty()) {
        return coreClassFactory<collector>::instance()->createObject(type);
    }
    return coreClassFactory<collector>::instance()->createObject(type, name);
}

}  // namespace griddyn
