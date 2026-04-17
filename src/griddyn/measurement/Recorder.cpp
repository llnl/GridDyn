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
    if (!fileName_.empty()) {
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
    nrec->fileName_ = fileName_;
    nrec->directory_ = directory_;
    nrec->binaryFile = binaryFile;
    nrec->precision = precision;
    nrec->autosave = autosave;
}

void Recorder::set(const std::string& param, double val)
{
    if (param == "precision") {
        precision = static_cast<decltype(precision)>(val);
    } else if ((param == "reserve") || (param == "reservecount")) {
        dataset.reserve(static_cast<fsize_t>(val));
    } else if (param == "autosave") {
        autosave = static_cast<count_t>(val);
    } else {
        collector::set(param, val);
    }
}

void Recorder::set(const std::string& param, const std::string& val)
{
    if ((param == "file") || (param == "fileName")) {
        fileName_ = val;
        std::filesystem::path filePath(fileName_);
        std::string ext = gmlc::utilities::convertToLowerCase(filePath.extension().string());
        binaryFile = ((ext != ".csv") && (ext != ".txt"));
    } else if (param == "directory") {
        directory_ = val;
    } else {
        collector::set(param, val);
    }
}

void Recorder::saveFile(const std::string& fileName)
{
    bool bFile = binaryFile;
    std::filesystem::path savefileName(fileName_);
    if (!fileName.empty()) {
        savefileName = std::filesystem::path(fileName);

        std::string ext = gmlc::utilities::convertToLowerCase(savefileName.extension().string());
        bFile = ((ext != ".csv") && (ext != ".txt"));
    } else {
        if (triggerTime == lastSaveTime) {
            return;  // no work todo
        }
    }
    if (!savefileName.empty()) {
        if (!directory_.empty()) {
            if (!savefileName.has_root_directory()) {
                savefileName = std::filesystem::path(directory_) / savefileName;
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
        if (recheck) {
            recheckColumns();
        }
        dataset.description = getName() + ": " + getDescription();
        bool append = (lastSaveTime > negTime);

        // create the file based on extension
        if (bFile) {
            dataset.writeBinaryFile(savefileName.string(), append);
        } else {
            dataset.writeTextFile(savefileName.string(), precision, append);
        }
        lastSaveTime = triggerTime;
    } else {
        throw(invalidFileName());
    }
}

void Recorder::reset()
{
    dataset.clear();
}
void Recorder::setSpace(coreTime span)
{
    auto pts = static_cast<count_t>(std::floor(span / timePeriod));
    dataset.reserve(pts + 1);
}

void Recorder::addSpace(coreTime span)
{
    auto pts = static_cast<count_t>(std::floor(span / timePeriod));
    dataset.reserve(static_cast<fsize_t>(dataset.time().capacity()) + pts + 1);
}

void Recorder::fillDatasetFields()
{
    dataset.setFields(collector::getColumnDescriptions());
}
change_code Recorder::trigger(coreTime time)
{
    collector::trigger(time);
    if (firstTrigger) {
        dataset.setCols(static_cast<fsize_t>(data.size()));
        fillDatasetFields();
        firstTrigger = false;
    }
    dataset.addData(time, data);
    if ((autosave > 0) && (static_cast<count_t>(dataset.size()) >= autosave)) {
        saveFile();
        dataset.clear();
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
