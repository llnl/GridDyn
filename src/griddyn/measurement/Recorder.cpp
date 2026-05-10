/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Recorder.h"

#include "core/coreExceptions.h"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include <filesystem>
#include <memory>
#include <string>

namespace griddyn {
using gmlc::utilities::fsize_t;

Recorder::Recorder(coreTime time0, coreTime period): collector(time0, period) {}
Recorder::Recorder(const std::string& name): collector(name) {}
Recorder::~Recorder()
{
    // check to make sure there is no unrecorded data
    if (!mFileName.empty()) {
        try {
            saveFile();
        }
        catch (const std::exception& e)  // no exceptions in a destructor
        {
            collector::getObject()->log(collector::getObject(), print_level::error, e.what());
        }
    }
}

std::unique_ptr<collector> Recorder::clone() const
{
    std::unique_ptr<collector> col = std::make_unique<Recorder>();
    cloneTo(col.get());
    return col;
}

void Recorder::cloneTo(collector* col) const
{
    collector::cloneTo(col);
    auto nrec = dynamic_cast<Recorder*>(col);
    if (nrec == nullptr) {
        return;
    }
    nrec->mFileName = mFileName;
    nrec->mDirectory = mDirectory;
    nrec->mBinaryFile = mBinaryFile;
    nrec->mPrecision = mPrecision;
    nrec->mAutosave = mAutosave;
}

void Recorder::set(std::string_view param, double val)
{
    if (param == "precision") {
        mPrecision = static_cast<decltype(mPrecision)>(val);
    } else if ((param == "reserve") || (param == "reservecount")) {
        mDataset.reserve(static_cast<fsize_t>(val));
    } else if (param == "autosave") {
        mAutosave = static_cast<count_t>(val);
    } else {
        collector::set(param, val);
    }
}

void Recorder::set(std::string_view param, std::string_view val)
{
    if ((param == "file") || (param == "fileName")) {
        mFileName = val;
        std::filesystem::path filePath(mFileName);
        std::string ext = gmlc::utilities::convertToLowerCase(filePath.extension().string());
        mBinaryFile = ((ext != ".csv") && (ext != ".txt"));
    } else if (param == "directory") {
        mDirectory = val;
    } else {
        collector::set(param, val);
    }
}

void Recorder::saveFile(const std::string& fileName)
{
    bool bFile = mBinaryFile;
    std::filesystem::path savefileName(mFileName);
    if (!fileName.empty()) {
        savefileName = std::filesystem::path(fileName);

        std::string ext = gmlc::utilities::convertToLowerCase(savefileName.extension().string());
        bFile = ((ext != ".csv") && (ext != ".txt"));
    } else {
        if (mTriggerTime == mLastSaveTime) {
            return;  // no work todo
        }
    }
    if (!savefileName.empty()) {
        if (!mDirectory.empty()) {
            if (!savefileName.has_root_directory()) {
                savefileName = std::filesystem::path(mDirectory) / savefileName;
            }
        }
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
        bool append = (mLastSaveTime > negTime);

        // create the file based on extension
        if (bFile) {
            mDataset.writeBinaryFile(savefileName.string(), append);
        } else {
            mDataset.writeTextFile(savefileName.string(), mPrecision, append);
        }
        mLastSaveTime = mTriggerTime;
    } else {
        throw(invalidFileName());
    }
}

void Recorder::reset()
{
    mDataset.clear();
}
void Recorder::setSpace(coreTime span)
{
    auto pts = static_cast<count_t>(std::floor(span / mTimePeriod));
    mDataset.reserve(pts + 1);
}

void Recorder::addSpace(coreTime span)
{
    auto pts = static_cast<count_t>(std::floor(span / mTimePeriod));
    mDataset.reserve(static_cast<fsize_t>(mDataset.time().capacity()) + pts + 1);
}

void Recorder::fillDatasetFields()
{
    mDataset.setFields(collector::getColumnDescriptions());
}
change_code Recorder::trigger(coreTime time)
{
    collector::trigger(time);
    if (mFirstTrigger) {
        mDataset.setCols(static_cast<fsize_t>(mData.size()));
        fillDatasetFields();
        mFirstTrigger = false;
    }
    mDataset.addData(time, mData);
    if ((mAutosave > 0) && (static_cast<count_t>(mDataset.size()) >= mAutosave)) {
        saveFile();
        mDataset.clear();
    }
    return change_code::no_change;
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
