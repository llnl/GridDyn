/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Recorder.h"

#include "core/CoreExceptions.h"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
using gmlc::utilities::fsize_t;

Recorder::Recorder(CoreTime time0, CoreTime period): Collector(time0, period) {}
Recorder::Recorder(const std::string& name): Collector(name) {}
Recorder::~Recorder()
{
    // check to make sure there is no unrecorded data
    if (!mFileName.empty() && std::cmp_greater(mDataset.size(), mSavedSamples)) {
        try {
            saveFile();
        }
        catch (const std::exception& e)  // no exceptions in a destructor
        {
            if (auto* object = getObject()) {
                object->log(object, PrintLevel::ERROR, e.what());
            } else {
                static_cast<void>(std::fputs(e.what(), stderr));
                static_cast<void>(std::fputc('\n', stderr));
            }
        }
    }
}

std::unique_ptr<Collector> Recorder::clone() const
{
    std::unique_ptr<Collector> col = std::make_unique<Recorder>();
    cloneTo(col.get());
    return col;
}

void Recorder::cloneTo(Collector* col) const
{
    if (col == this) {
        return;
    }
    Collector::cloneTo(col);
    auto* nrec = dynamic_cast<Recorder*>(col);
    if (nrec == nullptr) {
        return;
    }
    nrec->mFileName = mFileName;
    nrec->mDirectory = mDirectory;
    nrec->mPrecision = mPrecision;
    nrec->mAutosave = mAutosave;
    nrec->reset();
}

void Recorder::set(std::string_view param, double val)
{
    if (param == "precision") {
        if (!std::isfinite(val) || val < -1.0 || val > 17.0 || std::floor(val) != val) {
            throw InvalidParameterValue("recorder precision must be an integer from -1 to 17");
        }
        mPrecision = static_cast<decltype(mPrecision)>(val);
    } else if ((param == "reserve") || (param == "reservecount")) {
        if (!std::isfinite(val) || val < 0.0 || val > std::numeric_limits<fsize_t>::max() ||
            std::floor(val) != val) {
            throw InvalidParameterValue("invalid recorder reserve count");
        }
        mDataset.reserve(static_cast<fsize_t>(val));
    } else if (param == "autosave") {
        if (!std::isfinite(val) || val < 0.0 || val > std::numeric_limits<count_t>::max() ||
            std::floor(val) != val) {
            throw InvalidParameterValue("invalid recorder autosave count");
        }
        mAutosave = static_cast<count_t>(val);
    } else {
        Collector::set(param, val);
    }
}

void Recorder::set(std::string_view param, std::string_view val)
{
    if ((param == "file") || (param == "fileName") || (param == "filename")) {
        if (mFileName != val) {
            mLastSaveTime = negTime;
            mSavedSamples = 0;
        }
        mFileName = val;
    } else if (param == "directory") {
        if (mDirectory != val) {
            mLastSaveTime = negTime;
            mSavedSamples = 0;
        }
        mDirectory = val;
    } else {
        Collector::set(param, val);
    }
}

void Recorder::saveFile(const std::string& fileName)
{
    std::filesystem::path savefileName(fileName.empty() ? mFileName : fileName);
    if (!savefileName.empty()) {
        if (!mDirectory.empty()) {
            if (!savefileName.has_root_directory()) {
                savefileName = std::filesystem::path(mDirectory) / savefileName;
            }
        }
        bool configuredSink = fileName.empty();
        if (!configuredSink && !mFileName.empty()) {
            std::filesystem::path sink(mFileName);
            if (!mDirectory.empty() && !sink.has_root_directory()) {
                sink = std::filesystem::path(mDirectory) / sink;
            }
            std::error_code error;
            configuredSink = std::filesystem::equivalent(savefileName, sink, error) ||
                std::filesystem::absolute(savefileName).lexically_normal() ==
                    std::filesystem::absolute(sink).lexically_normal();
        }
        if (configuredSink && mDataset.size() == mSavedSamples && mLastSaveTime > negTime) {
            return;
        }
        const std::string extension = gmlc::utilities::convertToLowerCase(
            configuredSink ? std::filesystem::path(mFileName).extension().string() :
                             savefileName.extension().string());
        const bool binaryFile = extension != ".csv" && extension != ".txt";
        // check to make sure the directories exist if not create them
        auto tmp = savefileName.parent_path();
        if (!tmp.empty()) {
            if (!std::filesystem::exists(tmp)) {
                std::filesystem::create_directories(tmp);
            }
        }
        // recheck the columns if necessary
        if (mRecheck) {
            recheckColumns();
        }
        mDataset.description = getName() + ": " + getDescription();
        const bool append = configuredSink && (mLastSaveTime > negTime);

        // Explicit filenames export the current buffer as an independent snapshot.
        // For the configured sink, append only rows not saved by an earlier flush.
        gmlc::utilities::TimeSeriesMulti<double, CoreTime> pending;
        auto* dataset = &mDataset;
        if (append && mSavedSamples > 0) {
            pending.setCols(mDataset.columns());
            pending.setFields(mDataset.getFields());
            pending.reserve(mDataset.size() - static_cast<fsize_t>(mSavedSamples));
            std::vector<double> row(mDataset.columns());
            for (size_t index = mSavedSamples; index < mDataset.size(); ++index) {
                for (fsize_t column = 0; column < mDataset.columns(); ++column) {
                    row[column] = mDataset.data(column, static_cast<fsize_t>(index));
                }
                pending.addData(mDataset.time()[index], row);
            }
            dataset = &pending;
        }

        // create the file based on extension
        if (binaryFile) {
            dataset->writeBinaryFile(savefileName.string(), append);
        } else {
            dataset->writeTextFile(savefileName.string(), mPrecision, append);
        }
        if (configuredSink) {
            mLastSaveTime = mLastTriggerTime;
            mSavedSamples = mDataset.size();
        }
    } else {
        throw(InvalidFileName());
    }
}

void Recorder::reset()
{
    mDataset.clear();
    mLastSaveTime = negTime;
    mSavedSamples = 0;
    mFirstTrigger = true;
    mLayoutChanged = false;
    resetSchedule();
}
void Recorder::setSpace(CoreTime span)
{
    if (!std::isfinite(span) || span < 0.0 ||
        span / mTimePeriod >= std::numeric_limits<count_t>::max()) {
        throw InvalidParameterValue("invalid recorder reserve span");
    }
    auto pts = static_cast<count_t>(std::floor(span / mTimePeriod));
    mDataset.reserve(pts + 1);
}

void Recorder::addSpace(CoreTime span)
{
    if (!std::isfinite(span) || span < 0.0 ||
        span / mTimePeriod + mDataset.time().capacity() >= std::numeric_limits<fsize_t>::max()) {
        throw InvalidParameterValue("invalid recorder reserve span");
    }
    auto pts = static_cast<count_t>(std::floor(span / mTimePeriod));
    mDataset.reserve(static_cast<fsize_t>(mDataset.time().capacity()) + pts + 1);
}

void Recorder::fillDatasetFields()
{
    mDataset.setFields(Collector::getColumnDescriptions());
}

void Recorder::dataPointAdded(const CollectorPoint& /*point*/)
{
    mLayoutChanged = !mFirstTrigger;
}

void Recorder::updateObject(CoreObject* object, ObjectUpdateMode mode)
{
    mLayoutChanged = !mFirstTrigger;
    Collector::updateObject(object, mode);
}

ChangeCode Recorder::trigger(CoreTime time)
{
    if (!mArmed || time < mStartTime || time > mStopTime) {
        return ChangeCode::NO_CHANGE;
    }
    if (mLayoutChanged) {
        throw InvalidParameterValue("reset recorder before changing its signals or targets");
    }
    Collector::trigger(time);
    if (!mFirstTrigger && !std::cmp_equal(mDataset.columns(), mData.size())) {
        throw InvalidParameterValue("reset recorder before changing its column layout");
    }
    if (mFirstTrigger) {
        mDataset.setCols(static_cast<fsize_t>(mData.size()));
        fillDatasetFields();
        mFirstTrigger = false;
    }
    mDataset.addData(time, mData);
    if ((mAutosave > 0) && std::cmp_greater_equal(mDataset.size(), mAutosave)) {
        saveFile();
        mDataset.clear();
        mSavedSamples = 0;
    }
    return ChangeCode::NO_CHANGE;
}

void Recorder::flush()
{
    saveFile();
}
const std::string& Recorder::getSinkName() const
{
    return getFileName();
}
}  // namespace griddyn
