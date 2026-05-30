/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FileSource.h"

#include "core/CoreObjectTemplates.hpp"
#include <string>

namespace griddyn::sources {
FileSource::FileSource(const std::string& fileName, int column): RampSource("filesource_#")
{
    if (!fileName.empty()) {
        setFile(fileName, column);
    }
}

CoreObject* FileSource::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<FileSource, RampSource>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    if (!fileName_.empty()) {
        nobj->setFile(fileName_, m_column);
    }
    return nobj;
}

int FileSource::setFile(const std::string& fileName, index_t column)
{
    fileName_ = fileName;
    m_column = column;
    count = loadFile();
    currIndex = 0;
    return count;
}

void FileSource::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    prevTime = time0;
    if (opFlags[USE_ABSOLUTE_TIME_FLAG]) {
        double abstime0 = get("abstime0");
        index_t ii = 0;
        while (schedLoad.time(ii) < abstime0) {
            ++ii;
            if (ii >= static_cast<index_t>(
                          schedLoad.size())) {  // this should never happen (time would need to
                                                // get to a very very large number
                ii = schedLoad.size();
                break;
            }
        }
        currIndex = ii;
        nextUpdateTime = schedLoad.time(currIndex);
        timestep(time0, noInputs, cLocalSolverMode);
    } else {
        if (schedLoad.time(currIndex) < time0) {
            while (schedLoad.time(currIndex) < time0) {
                currIndex++;
            }
            currIndex = currIndex - 1;
            nextUpdateTime = schedLoad.time(currIndex);
            timestep(time0, noInputs, cLocalSolverMode);
        }
    }
    return RampSource::dynObjectInitializeA(time0, flags);
}

void FileSource::updateA(CoreTime time)
{
    while (time >= schedLoad.time(currIndex)) {
        m_output = schedLoad.data(currIndex);
        prevTime = schedLoad.time(currIndex);
        currIndex++;
        if (currIndex >=
            count) {  // this should never happen since the last time should be very large
            currIndex = count;
            mp_dOdt = 0;
            break;
        }

        if (opFlags[USE_STEP_CHANGE_FLAG]) {
            mp_dOdt = 0;
        } else {
            double diff = schedLoad.data(currIndex) - m_output;
            double dt = schedLoad.time(currIndex) - schedLoad.time(currIndex - 1);
            mp_dOdt = diff / dt;
        }

        nextUpdateTime = schedLoad.time(currIndex);
    }
}

void FileSource::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    if (time > nextUpdateTime) {
        updateA(time);
    }

    RampSource::timestep(time, inputs, sMode);
}

void FileSource::setFlag(std::string_view flag, bool val)
{
    if (flag == "absolute") {
        opFlags.set(USE_ABSOLUTE_TIME_FLAG, val);
    } else if (flag == "relative") {
        opFlags.set(USE_ABSOLUTE_TIME_FLAG, !val);
    } else if (flag == "step") {
        opFlags.set(USE_STEP_CHANGE_FLAG, val);
    } else if (flag == "interpolate") {
        opFlags.set(USE_STEP_CHANGE_FLAG, !val);
    } else {
        RampSource::setFlag(flag, val);
    }
}
void FileSource::set(std::string_view param, std::string_view val)
{
    if ((param == "fileName") || (param == "file")) {
        setFile(std::string{val}, 0);
    } else {
        Source::set(param, val);
    }
}

void FileSource::set(std::string_view param, double val, units::unit unitType)
{
    {
        Source::set(param, val, unitType);
    }
}

int FileSource::loadFile()
{
    auto stl = fileName_.length();
    // TODO(phlpt): Use the filesystem library to check the extension instead of this.
    if ((stl > 4) && ((fileName_[stl - 3] == 'c') || (fileName_[stl - 3] == 't'))) {
        schedLoad.loadTextFile(fileName_, m_column);
    } else {
        schedLoad.loadBinaryFile(fileName_, m_column);
    }
    if (!schedLoad.empty()) {
        schedLoad.addData(schedLoad.lastTime() + 365.0 * kDayLength, schedLoad.lastData());
    } else {
        schedLoad.addData(365.0 * kDayLength, m_output);
    }
    return schedLoad.size();
}
}  // namespace griddyn::sources
