/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Recorder.h"

#include "core/CoreExceptions.h"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "utilities/Hdf5DataFile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::fsize_t;

namespace {
    using RecorderSamples = gmlc::utilities::TimeSeriesMulti<double, CoreTime>;

    void writeRecorderText(std::ostream& output,
                           const RecorderSamples& samples,
                           fsize_t firstSample,
                           int precision,
                           bool append)
    {
        if (!append) {
            const auto description =
                gmlc::utilities::stringOps::characterReplace(samples.description, '\n', "\n#");
            output << "# " << description << "\n\"time\"";
            for (const auto& field : samples.getFields()) {
                output << R"(, ")" << field << '"';
            }
            output << '\n';
        }
        const int valuePrecision = (precision < 1) ? 8 : precision;
        for (fsize_t row = firstSample; row < samples.size(); ++row) {
            output << std::setprecision(5) << samples.time()[row];
            for (fsize_t col = 0; col < samples.columns(); ++col) {
                output << ',' << std::setprecision(valuePrecision) << samples.data(col, row);
            }
            output << '\n';
        }
    }

    void writeRecorderBinary(std::ostream& output,
                             const RecorderSamples& samples,
                             fsize_t firstSample,
                             bool append)
    {
        // Match the legacy reader's layout. Each block contains its dimensions
        // followed by the times and values in column order.
        const auto writeValue = [&output](const auto& value) {
            output.write(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        const auto sampleCount = samples.size() - firstSample;
        if (!append) {
            writeValue(fsize_t{1});
            const auto descriptionLength = static_cast<fsize_t>(samples.description.size());
            writeValue(descriptionLength);
            output.write(samples.description.data(), descriptionLength);
        }
        writeValue(sampleCount);
        writeValue(samples.columns() + fsize_t{1});
        if (!append) {
            for (const auto& field : samples.getFields()) {
                const auto length = static_cast<unsigned char>(std::min(field.size(), size_t{255}));
                writeValue(length);
                output.write(field.data(), length);
            }
        }
        for (const auto time : std::span(samples.time()).subspan(firstSample, sampleCount)) {
            writeValue(static_cast<double>(time));
        }
        if (sampleCount != 0) {
            for (fsize_t col = 0; col < samples.columns(); ++col) {
                const auto values = std::span(samples.data(col)).subspan(firstSample, sampleCount);
                output.write(reinterpret_cast<const char*>(values.data()),
                             static_cast<std::streamsize>(values.size_bytes()));
            }
        }
    }
}  // namespace

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
    nrec->mHdf5Compression = mHdf5Compression;
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
    } else if (param == "compression") {
        if (!std::isfinite(val) || val < 0.0 || val > 9.0 || std::floor(val) != val) {
            throw InvalidParameterValue("HDF5 compression must be an integer from 0 to 9");
        }
        if (mHdf5Started && val != mHdf5Compression) {
            throw InvalidParameterValue("reset recorder before changing HDF5 compression");
        }
        mHdf5Compression = static_cast<unsigned>(val);
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
            resetOutput();
        }
        mFileName = val;
    } else if (param == "directory") {
        if (mDirectory != val) {
            resetOutput();
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
        if (configuredSink && mDataset.size() == mSavedSamples &&
            (mLastSaveTime > negTime || mHdf5Started)) {
            return;
        }
        const std::string extension = gmlc::utilities::convertToLowerCase(
            configuredSink ? std::filesystem::path(mFileName).extension().string() :
                             savefileName.extension().string());
        const bool binaryFile = extension != ".csv" && extension != ".txt";
        const bool hdf5File = extension == ".h5" || extension == ".hdf5";
        // Create missing directories and let the writer report other path errors.
        const auto parent = savefileName.parent_path();
        std::error_code directoryError;
        if (!parent.empty() &&
            std::filesystem::status(parent, directoryError).type() ==
                std::filesystem::file_type::not_found) {
            std::filesystem::create_directories(parent, directoryError);
            if (directoryError) {
                throw FileOperationError(std::format("unable to create recorder directory '{}': {}",
                                                     parent.string(),
                                                     directoryError.message()));
            }
        }
        // recheck the columns if necessary
        if (mRecheck) {
            recheckColumns();
        }
        mDataset.description = getName() + ": " + getDescription();
        if (hdf5File) {
            if (mFirstTrigger) {
                mDataset.setCols(static_cast<fsize_t>(mData.size()));
                fillDatasetFields();
            }
            try {
                saveHdf5(savefileName.string(), configuredSink);
            }
            catch (const std::runtime_error& error) {
                throw FileOperationError(std::format("unable to write recorder file '{}': {}",
                                                     savefileName.string(),
                                                     error.what()));
            }
            if (configuredSink) {
                mLastSaveTime = mLastTriggerTime;
                mSavedSamples = mDataset.size();
            }
            return;
        }
        const bool append = configuredSink && (mLastSaveTime > negTime);

        if (append && mUnsafeAppend) {
            throw FileOperationError(
                std::format("recorder output '{}' has an incomplete append; change the destination "
                            "or reset the recorder before writing again",
                            savefileName.string()));
        }
        // A failed append can leave part of a row or binary block in the file.
        std::error_code sizeError;
        const auto previousSize = append ? std::filesystem::file_size(savefileName, sizeError) : 0;
        const auto mode = (append ? std::ios::app : std::ios::trunc) |
            (binaryFile ? std::ios::binary : std::ios::out);
        std::ofstream output(savefileName, mode);
        if (!output) {
            throw FileOperationError(
                std::format("unable to open recorder file '{}'", savefileName.string()));
        }
        const auto firstSample = append ? static_cast<fsize_t>(mSavedSamples) : fsize_t{0};
        if (binaryFile) {
            writeRecorderBinary(output, mDataset, firstSample, append);
        } else {
            writeRecorderText(output, mDataset, firstSample, mPrecision, append);
        }
        output.close();
        if (!output) {
            std::error_code rollbackError;
            if (append && !sizeError) {
                std::filesystem::resize_file(savefileName, previousSize, rollbackError);
            }
            if (rollbackError) {
                mUnsafeAppend = true;
                throw FileOperationError(std::format(
                    "unable to write recorder file '{}' or restore its previous size: {}",
                    savefileName.string(),
                    rollbackError.message()));
            }
            throw FileOperationError(
                std::format("unable to write recorder file '{}'", savefileName.string()));
        }
        if (configuredSink) {
            mLastSaveTime = mLastTriggerTime;
            mSavedSamples = mDataset.size();
        }
    } else {
        throw InvalidFileName("no recorder output file specified");
    }
}

void Recorder::resetOutput()
{
    mHdf5Samples = 0;
    mHdf5Started = false;
    mUnsafeAppend = false;
    mLastSaveTime = negTime;
    mSavedSamples = 0;
}

void Recorder::saveHdf5(const std::string& fileName, bool configuredSink)
{
    using utilities::Hdf5DataFile;
    using utilities::Hdf5RecordType;
    using utilities::Hdf5RecordView;
    constexpr std::uint32_t recorderSource = 3;
    const auto first = configuredSink ? mSavedSamples : 0;
    const auto rows = mDataset.size() - first;
    const auto columns = mDataset.columns();
    const auto sampleOffset = configuredSink ? mHdf5Samples : 0;
    const bool header = !configuredSink || !mHdf5Started;
    std::vector<double> values;
    if ((columns > 0 && rows > values.max_size() / columns) ||
        rows > utilities::hdf5NoIndex - sampleOffset) {
        throw FileOperationError("recorder HDF5 sample size overflow");
    }
    values.resize(rows * columns);
    std::vector<Hdf5RecordView> records;
    records.reserve(rows + (header ? 2 : 0));
    const std::array<std::string, 2> description{getName(), getDescription()};
    if (header) {
        Hdf5RecordView names;
        names.info.source = recorderSource;
        names.info.type = Hdf5RecordType::NAMES;
        names.info.key = 0;
        names.info.code = 1;
        names.names = mDataset.getFields();
        records.push_back(names);
        names.info.code = 2;
        names.names = description;
        records.push_back(names);
    }
    for (std::size_t row = 0; row < rows; ++row) {
        for (fsize_t column = 0; column < columns; ++column) {
            values[(row * columns) + column] =
                mDataset.data(column, static_cast<fsize_t>(first + row));
        }
        Hdf5RecordView sample;
        sample.info.source = recorderSource;
        sample.info.key = 0;
        sample.info.index = sampleOffset + row;
        sample.info.time_ns = mDataset.time()[first + row].to_ns().count();
        sample.values = std::span(values).subspan(row * columns, columns);
        records.push_back(sample);
    }
    if (records.empty()) {
        return;
    }
    Hdf5DataFile output(fileName,
                        header ? Hdf5DataFile::OpenMode::TRUNCATE : Hdf5DataFile::OpenMode::APPEND,
                        mHdf5Compression);
    output.append(records);
    if (configuredSink) {
        mHdf5Started = true;
        mHdf5Samples += rows;
    }
}

void Recorder::reset()
{
    mDataset.clear();
    resetOutput();
    mFirstTrigger = true;
    mLayoutChanged = false;
    resetSchedule();
}
void Recorder::setSpace(CoreTime span)
{
    if (span < 0.0 || span >= maxTime ||
        span / mTimePeriod >= std::numeric_limits<count_t>::max()) {
        throw InvalidParameterValue("invalid recorder reserve span");
    }
    auto pts = static_cast<count_t>(std::floor(span / mTimePeriod));
    mDataset.reserve(pts + 1);
}

void Recorder::addSpace(CoreTime span)
{
    if (span < 0.0 || span >= maxTime ||
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
    mLayoutChanged = !mFirstTrigger || mHdf5Started;
}

void Recorder::updateObject(CoreObject* object, ObjectUpdateMode mode)
{
    mLayoutChanged = !mFirstTrigger || mHdf5Started;
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
