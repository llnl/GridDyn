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

static classFactory<collector> collFac("collector");

static childClassFactory<Recorder, collector>
    grFac(std::vector<std::string>{"recorder", "rec", "file"}, "recorder");

collector::collector(coreTime time0, coreTime period):
    timePeriod(period), reqPeriod(period), triggerTime(time0)
{
}

collector::collector(const std::string& collectorName):
    helperObject(collectorName), timePeriod(1.0), reqPeriod(1.0), triggerTime(timeZero)
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
    col->reqPeriod = reqPeriod;
    col->timePeriod = timePeriod;
    col->setName(getName());
    col->startTime = startTime;
    col->stopTime = stopTime;
    col->triggerTime = triggerTime;
    col->lastTriggerTime = lastTriggerTime;
    for (size_t kk = 0; kk < points.size(); ++kk) {
        if (kk >= col->points.size()) {
            auto ggb = (points[kk].dataGrabber) ? points[kk].dataGrabber->clone() : nullptr;
            auto ggbst = (points[kk].dataGrabberSt) ? points[kk].dataGrabberSt->clone() : nullptr;
            col->points.emplace_back(std::move(ggb), std::move(ggbst), points[kk].column);
        } else {
            if (col->points[kk].dataGrabber) {
                if (points[kk].dataGrabber) {
                    points[kk].dataGrabber->cloneTo(col->points[kk].dataGrabber.get());
                }
            } else if (points[kk].dataGrabber) {
                col->points[kk].dataGrabber = points[kk].dataGrabber->clone();
            }

            if (col->points[kk].dataGrabberSt) {
                if (points[kk].dataGrabberSt) {
                    points[kk].dataGrabberSt->cloneTo(col->points[kk].dataGrabberSt.get());
                }
            } else if (points[kk].dataGrabberSt) {
                col->points[kk].dataGrabberSt = points[kk].dataGrabberSt->clone();
            }

            col->points[kk].column = points[kk].column;
        }
    }
    col->data.resize(data.size());
}

void collector::updateObject(coreObject* gco, object_update_mode mode)
{
    for (const auto& gg : points) {
        if (gg.dataGrabber) {
            gg.dataGrabber->updateObject(gco, mode);
            if (gg.dataGrabber->vectorGrab) {
                recheck = true;
            }
        } else if (gg.dataGrabberSt) {
            gg.dataGrabberSt->updateObject(gco, mode);
        }
    }
}

coreObject* collector::getObject() const
{
    if (!points.empty()) {
        if (points[0].dataGrabber) {
            return points.front().dataGrabber->getObject();
        }
        if (points[0].dataGrabberSt) {
            return points.front().dataGrabberSt->getObject();
        }
    }
    return nullptr;
}

void collector::getObjects(std::vector<coreObject*>& objects) const
{
    for (const auto& gg : points) {
        if (gg.dataGrabber) {
            gg.dataGrabber->getObjects(objects);
        } else if (gg.dataGrabberSt) {
            gg.dataGrabberSt->getObjects(objects);
        }
    }
}

std::vector<std::string> collector::getColumnDescriptions() const
{
    stringVec res;
    res.resize(data.size());
    for (const auto& datapoint : points) {
        if (datapoint.dataGrabber->vectorGrab) {
            stringVec vdesc;
            datapoint.dataGrabber->getDesc(vdesc);
            for (size_t kk = 0; kk < vdesc.size(); ++kk) {
                if ((datapoint.colname.empty()) || (!vectorName)) {
                    res[datapoint.column + kk] = vdesc[kk];
                } else {
                    res[datapoint.column + kk] = datapoint.colname + "[" + std::to_string(kk) + "]";
                }
            }
        } else {
            if (datapoint.colname.empty()) {
                res[datapoint.column] = datapoint.dataGrabber->getDesc();
            } else {
                res[datapoint.column] = datapoint.colname;
            }
        }
    }
    return res;
}

void collector::set(std::string_view param, double val)
{
    if (param == "period") {
        reqPeriod = val;
        timePeriod = val;
    } else if (param == "frequency") {
        reqPeriod = 1.0 / val;
        timePeriod = val;
    } else if ((param == "triggertime") || (param == "trigger") || (param == "time")) {
        triggerTime = val;
    } else if ((param == "starttime") || (param == "start")) {
        startTime = val;
        triggerTime = startTime;
    } else if ((param == "stoptime") || (param == "stop")) {
        stopTime = val;
    } else if (param == "period_resolution") {
        if (val > 0) {
            auto per = static_cast<int>(std::round(reqPeriod / val));
            timePeriod = (per == 0) ? val : val * per;
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
        vectorName = val;
    } else {
        helperObject::setFlag(flag, val);
    }
}

void collector::setTime(coreTime time)
{
    if (time > triggerTime) {
        triggerTime = time;
    }
}

void collector::recheckColumns()
{
    fsize_t ct = 0;
    std::vector<double> vals;
    // for (size_t kk = 0; kk < points.size(); ++kk)
    for (auto& pt : points) {
        if (pt.column == -1) {
            pt.column = ct;
        }

        if (pt.dataGrabber->vectorGrab) {
            pt.dataGrabber->grabVectorData(vals);
            ct += static_cast<fsize_t>(vals.size());
        } else {
            ++ct;
        }
    }
    data.resize(ct);
    recheck = false;
}

count_t collector::grabData(double* outputData, index_t N)
{
    std::vector<double> vals;
    count_t currentCount = 0;
    if (N <= 0) {
        return 0;
    }
    const auto outputLimit = static_cast<count_t>(N);
    if (recheck) {
        recheckColumns();
    }
    for (auto& datapoint : points) {
        if (datapoint.dataGrabber->vectorGrab) {
            datapoint.dataGrabber->grabVectorData(vals);
            if (datapoint.column < 0) {
                continue;
            }
            const auto column = static_cast<count_t>(datapoint.column);
            if (column < outputLimit) {
                const auto remaining = outputLimit - column;
                const auto valueCount = static_cast<count_t>(vals.size());
                const auto copyCount = std::min(remaining, valueCount);
                std::copy_n(vals.begin(), copyCount, outputData + column);
                const auto nextCount = column + copyCount;
                currentCount = std::max(currentCount, nextCount);
            }
        } else if (datapoint.column >= 0 && static_cast<count_t>(datapoint.column) < outputLimit) {
            const auto column = static_cast<count_t>(datapoint.column);
            outputData[column] = datapoint.dataGrabber->grabData();
            const auto nextCount = column + count_t{1};
            currentCount = std::max(currentCount, nextCount);
        }
    }
    currentCount = std::min(currentCount, outputLimit);
    return currentCount;
}

change_code collector::trigger(coreTime time)
{
    std::vector<double> vals;

    if (recheck) {
        recheckColumns();
    }
    for (auto& datapoint : points) {
        if (datapoint.dataGrabber->vectorGrab) {
            datapoint.dataGrabber->grabVectorData(vals);
            std::copy(vals.begin(), vals.end(), data.begin() + datapoint.column);
        } else {
            data[datapoint.column] = datapoint.dataGrabber->grabData();
        }
    }
    lastTriggerTime = time;
    int cnt = 0;
    while (time >= triggerTime) {
        triggerTime += timePeriod;
        ++cnt;
        if (cnt > 5) {
            triggerTime = time + timePeriod;
        }
    }
    if (triggerTime > stopTime) {
        triggerTime = maxTime;
    }
    return change_code::no_change;
}

int collector::getColumn(int requestedColumn) const
{
    int retColumn = requestedColumn;
    if (requestedColumn < 0) {
        if (recheck) {
            retColumn = -1;
        } else {
            retColumn = static_cast<int>(columns);
        }
    }
    return retColumn;
}

void collector::updateColumns(int requestedColumn)
{
    if (requestedColumn >= static_cast<int>(columns)) {
        columns = requestedColumn + 1;
    }

    if (!recheck) {
        data.resize(columns);
    }
}

// TODO(phlpt): Merge the repeated add-path code.
void collector::add(std::shared_ptr<gridGrabber> ggb, int requestedColumn)  // NOLINT
{
    int newColumn = getColumn(requestedColumn);

    if (ggb->vectorGrab) {
        recheck = true;
    }

    updateColumns(newColumn);
    points.emplace_back(ggb, nullptr, newColumn);
    if (!ggb->getDesc().empty()) {
        points.back().colname = ggb->getDesc();
    }
    dataPointAdded(points.back());
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
    int newColumn = getColumn(requestedColumn);
    updateColumns(newColumn);

    points.emplace_back(nullptr, sst, newColumn);

    dataPointAdded(points.back());
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
    int newColumn = getColumn(requestedColumn);
    updateColumns(newColumn);

    points.emplace_back(ggb, sst, newColumn);
    if (!ggb->getDesc().empty()) {
        points.back().colname = ggb->getDesc();
    }
    dataPointAdded(points.back());
    if ((!ggb->loaded) && (!sst->loaded)) {
        addWarning("grabber not loaded");
    }
}

// a notification that something was added much more useful in derived classes
void collector::dataPointAdded(const collectorPoint& /*cp*/) {}
void collector::add(const gridGrabberInfo& gdRI, coreObject* obj)
{
    if (gdRI.field.empty())  // any field specification overrides the offset
    {
        if (gdRI.offset > 0) {
            auto ggb = createGrabber(gdRI.offset, obj);
            // auto sst = makeStateGrabbers(gdRI.offset, obj);
            if (ggb) {
                ggb->bias = gdRI.bias;
                ggb->gain = gdRI.gain;
                return add(std::shared_ptr<gridGrabber>(std::move(ggb)), gdRI.column);
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
        auto v = gmlc::utilities::stringOps::splitlineBracket(gdRI.field, ",;");
        if (v.size() > 1) {
            int ccol = gdRI.column;
            auto tempInfo = gdRI;
            for (const auto& fld : v) {
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

void collector::add(std::string_view field, coreObject* obj)
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
    points.clear();
    data.clear();
    warnList.clear();
    warningCount = 0;
    triggerTime = maxTime;
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
