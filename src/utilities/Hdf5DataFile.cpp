/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Hdf5DataFile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <hdf5.h>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace utilities {
namespace {
    std::mutex& storageMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
    void check(bool valid, const char* message)
    {
        if (!valid) {
            throw std::runtime_error(std::string("GridDyn HDF5: ") + message);
        }
    }
    class Handle {
      public:
        hid_t mId = -1;
        herr_t (*mClose)(hid_t) = nullptr;
        Handle() = default;
        Handle(hid_t value, herr_t (*closer)(hid_t)): mId(value), mClose(closer)
        {
            check(mId >= 0, "unable to create/open an HDF5 object");
        }
        ~Handle()
        {
            if (mId >= 0) {
                mClose(mId);
            }
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& other) noexcept: mId(std::exchange(other.mId, -1)), mClose(other.mClose) {}
        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other) {
                if (mId >= 0) {
                    mClose(mId);
                }
                mId = std::exchange(other.mId, -1);
                mClose = other.mClose;
            }
            return *this;
        }
        operator hid_t() const { return mId; }
    };
    // Suppress only this thread's temporary error printer; retain the caller's policy.
    class ErrorScope {
        H5E_auto2_t mPrinter = nullptr;
        void* mData = nullptr;

      public:
        ErrorScope()
        {
            H5Eget_auto2(H5E_DEFAULT, &mPrinter, &mData);
            H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
        }
        ~ErrorScope() { H5Eset_auto2(H5E_DEFAULT, mPrinter, mData); }
    };
    std::uint64_t extent(hid_t dataset)
    {
        const Handle space(H5Dget_space(dataset), H5Sclose);
        hsize_t size = 0;
        check(H5Sget_simple_extent_ndims(space) == 1 &&
                  H5Sget_simple_extent_dims(space, &size, nullptr) >= 0,
              "expected a one-dimensional dataset");
        return size;
    }
    std::size_t checkedSize(std::uint64_t count, std::size_t elementSize)
    {
        check(count <= std::numeric_limits<std::size_t>::max() / elementSize, "array is too large");
        return static_cast<std::size_t>(count);
    }
    void range(std::uint64_t first, std::uint64_t count, std::uint64_t size)
    {
        check(first <= size && count <= size - first, "dataset range is invalid or incomplete");
    }
    void transfer(hid_t dataset,
                  hid_t type,
                  std::uint64_t first,
                  std::uint64_t count,
                  void* buffer,
                  bool write)
    {
        if (count == 0) {
            return;
        }
        const Handle fileSpace(H5Dget_space(dataset), H5Sclose);
        const hsize_t start = first;
        const hsize_t size = count;
        check(H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET, &start, nullptr, &size, nullptr) >= 0,
              "unable to select dataset range");
        const Handle memorySpace(H5Screate_simple(1, &size, nullptr), H5Sclose);
        check((write ? H5Dwrite(dataset, type, memorySpace, fileSpace, H5P_DEFAULT, buffer) :
                       H5Dread(dataset, type, memorySpace, fileSpace, H5P_DEFAULT, buffer)) >= 0,
              "dataset I/O failed");
    }
    void extend(hid_t dataset, std::uint64_t size)
    {
        const hsize_t end = size;
        check(H5Dset_extent(dataset, &end) >= 0, "unable to extend dataset");
    }
    Handle recordType(bool native)
    {
        Handle type(H5Tcreate(H5T_COMPOUND, sizeof(Hdf5RecordInfo)), H5Tclose);
#define FIELD(name, disk, memory)                                                                  \
    check(H5Tinsert(type, #name, offsetof(Hdf5RecordInfo, name), native ? (memory) : (disk)) >= 0, \
          "record type definition failed")
        FIELD(time_ns, H5T_STD_I64LE, H5T_NATIVE_INT64);
        FIELD(frame, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(index, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(key, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(paired_key, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(parent_index, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(value_offset, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(count, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(index_offset, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(byte_offset, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(byte_count, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(rows, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(columns, H5T_STD_U64LE, H5T_NATIVE_UINT64);
        FIELD(cj, H5T_IEEE_F64LE, H5T_NATIVE_DOUBLE);
        FIELD(source, H5T_STD_U32LE, H5T_NATIVE_UINT32);
        FIELD(type, H5T_STD_U32LE, H5T_NATIVE_UINT32);
        FIELD(code, H5T_STD_U32LE, H5T_NATIVE_UINT32);
        FIELD(flags, H5T_STD_U32LE, H5T_NATIVE_UINT32);
        FIELD(approx, H5T_STD_U32LE, H5T_NATIVE_UINT32);
#undef FIELD
        if (!native) {
            check(H5Tpack(type) >= 0, "unable to pack record type");
        }
        return type;
    }
    Handle
        createStream(hid_t file, const char* name, hid_t type, hsize_t chunk, unsigned compression)
    {
        const hsize_t zero = 0;
        const hsize_t unlimited = H5S_UNLIMITED;
        const Handle space(H5Screate_simple(1, &zero, &unlimited), H5Sclose);
        const Handle properties(H5Pcreate(H5P_DATASET_CREATE), H5Pclose);
        check(H5Pset_chunk(properties, 1, &chunk) >= 0, "unable to configure chunks");
        if (compression > 0) {
            check(H5Pset_shuffle(properties) >= 0 && H5Pset_deflate(properties, compression) >= 0,
                  "unable to configure lossless compression");
        }
        return {H5Dcreate2(file, name, type, space, H5P_DEFAULT, properties, H5P_DEFAULT),
                H5Dclose};
    }
    void scalarAttribute(hid_t file, const char* name, std::uint32_t value)
    {
        const Handle space(H5Screate(H5S_SCALAR), H5Sclose);
        const Handle attr(H5Acreate2(file, name, H5T_STD_U32LE, space, H5P_DEFAULT, H5P_DEFAULT),
                          H5Aclose);
        check(H5Awrite(attr, H5T_NATIVE_UINT32, &value) >= 0, "attribute write failed");
    }
}  // namespace

class Hdf5DataFile::Impl {
  public:
    Handle mFile;
    Handle mRecords;
    Handle mValues;
    Handle mIndices;
    Handle mBytes;
    Handle mCommitted;
    Handle mNativeRecord;
    std::uint64_t mCount = 0;
    std::uint64_t mFrame = 0;
    bool mWritable = false;
    bool mFailed = false;
    Impl(const std::string& path, OpenMode mode, unsigned compression):
        mNativeRecord(recordType(true))
    {
        check(compression <= 9, "compression must be between 0 and 9");
        mWritable = mode != OpenMode::READ_ONLY;
        const bool create = mode == OpenMode::TRUNCATE ||
            (mode == OpenMode::APPEND && !std::filesystem::exists(path));
        if (create) {
            const Handle access(H5Pcreate(H5P_FILE_ACCESS), H5Pclose);
            check(H5Pset_libver_bounds(access, H5F_LIBVER_V18, H5F_LIBVER_V18) >= 0,
                  "unable to set file feature bounds");
            mFile = Handle(H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, access), H5Fclose);
            const Handle textType(H5Tcopy(H5T_C_S1), H5Tclose);
            constexpr char format[] = "griddyn-data";
            check(H5Tset_size(textType, sizeof(format)) >= 0, "string type failed");
            const Handle scalar(H5Screate(H5S_SCALAR), H5Sclose);
            const Handle attr(
                H5Acreate2(mFile, "format", textType, scalar, H5P_DEFAULT, H5P_DEFAULT), H5Aclose);
            check(H5Awrite(attr, textType, format) >= 0, "format attribute write failed");
            scalarAttribute(mFile, "schema_version", 1);
            auto diskRecord = recordType(false);
            mRecords = createStream(mFile, "/records", diskRecord, 128, 0);
            mValues = createStream(mFile, "/values", H5T_IEEE_F64LE, 8192, compression);
            mIndices = createStream(mFile, "/indices", H5T_STD_U64LE, 8192, compression);
            mBytes = createStream(mFile, "/bytes", H5T_STD_U8LE, 65536, compression);
            mCommitted = Handle(H5Dcreate2(mFile,
                                           "/committed_records",
                                           H5T_STD_U64LE,
                                           scalar,
                                           H5P_DEFAULT,
                                           H5P_DEFAULT,
                                           H5P_DEFAULT),
                                H5Dclose);
            check(H5Dwrite(mCommitted, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &mCount) >=
                      0,
                  "completion initialization failed");
        } else {
            mFile = Handle(H5Fopen(path.c_str(),
                                   mWritable ? H5F_ACC_RDWR : H5F_ACC_RDONLY,
                                   H5P_DEFAULT),
                           H5Fclose);
            const Handle attr(H5Aopen(mFile, "format", H5P_DEFAULT), H5Aclose);
            const Handle formatSpace(H5Aget_space(attr), H5Sclose);
            check(H5Sget_simple_extent_npoints(formatSpace) == 1, "invalid format attribute shape");
            const Handle textType(H5Aget_type(attr), H5Tclose);
            check(H5Tget_class(textType) == H5T_STRING && H5Tis_variable_str(textType) == 0 &&
                      H5Tget_size(textType) == 13,
                  "unsupported format");
            std::array<char, 13> format{};
            check(H5Aread(attr, textType, format.data()) >= 0 &&
                      std::string_view(format.data(), 12) == "griddyn-data",
                  "unsupported format");
            const Handle versionAttr(H5Aopen(mFile, "schema_version", H5P_DEFAULT), H5Aclose);
            const Handle versionSpace(H5Aget_space(versionAttr), H5Sclose);
            check(H5Sget_simple_extent_npoints(versionSpace) == 1,
                  "invalid version attribute shape");
            std::uint32_t version = 0;
            check(H5Aread(versionAttr, H5T_NATIVE_UINT32, &version) >= 0 && version == 1,
                  "unsupported schema version");
            mRecords = Handle(H5Dopen2(mFile, "/records", H5P_DEFAULT), H5Dclose);
            mValues = Handle(H5Dopen2(mFile, "/values", H5P_DEFAULT), H5Dclose);
            mIndices = Handle(H5Dopen2(mFile, "/indices", H5P_DEFAULT), H5Dclose);
            mBytes = Handle(H5Dopen2(mFile, "/bytes", H5P_DEFAULT), H5Dclose);
            mCommitted = Handle(H5Dopen2(mFile, "/committed_records", H5P_DEFAULT), H5Dclose);
            const auto requireType = [](hid_t dataset, hid_t expected) {
                const Handle actual(H5Dget_type(dataset), H5Tclose);
                check(H5Tequal(actual, expected) > 0, "unsupported dataset datatype");
            };
            auto diskRecord = recordType(false);
            requireType(mRecords, diskRecord);
            requireType(mValues, H5T_IEEE_F64LE);
            requireType(mIndices, H5T_STD_U64LE);
            requireType(mBytes, H5T_STD_U8LE);
            requireType(mCommitted, H5T_STD_U64LE);
            const Handle scalar(H5Dget_space(mCommitted), H5Sclose);
            check(H5Sget_simple_extent_type(scalar) == H5S_SCALAR, "invalid completion dataset");
            check(H5Dread(mCommitted, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &mCount) >=
                      0,
                  "unable to read completion count");
            const auto stored = extent(mRecords);
            check(mCount <= stored && (!mWritable || mCount == stored), "incomplete record index");
            if (mCount > 0) {
                Hdf5RecordInfo last;
                transfer(mRecords, mNativeRecord, mCount - 1, 1, &last, false);
                check(last.frame != hdf5NoIndex, "frame number overflow");
                mFrame = last.frame + 1;
            }
        }
    }
};

Hdf5DataFile::Hdf5DataFile(const std::string& path, OpenMode mode, unsigned compression)
{
    const std::scoped_lock lock(storageMutex());
    const ErrorScope errors;
    mReadOnly = mode == OpenMode::READ_ONLY;
    if (!mReadOnly) {
        static std::unordered_map<std::string, std::weak_ptr<Impl>> writers;
        const auto key = std::filesystem::absolute(path).lexically_normal().string();
        std::erase_if(writers, [](const auto& entry) { return entry.second.expired(); });
        if (auto active = writers[key].lock()) {
            check(mode != OpenMode::TRUNCATE, "cannot truncate an active output session");
            mImpl = std::move(active);
            return;
        }
        mImpl = std::make_shared<Impl>(path,
                                       mode == OpenMode::CREATE_OR_JOIN ? OpenMode::TRUNCATE : mode,
                                       compression);
        writers[key] = mImpl;
    } else {
        mImpl = std::make_shared<Impl>(path, mode, compression);
    }
}
Hdf5DataFile::~Hdf5DataFile()
{
    const std::scoped_lock lock(storageMutex());
    mImpl.reset();
}
std::uint64_t Hdf5DataFile::recordCount() const
{
    const std::scoped_lock lock(storageMutex());
    return mImpl->mCount;
}

void Hdf5DataFile::append(std::span<const Hdf5RecordView> views)
{
    const std::scoped_lock lock(storageMutex());
    const ErrorScope errors;
    check(!mReadOnly && mImpl->mWritable && !mImpl->mFailed, "file is not writable");
    check(!views.empty() && mImpl->mFrame != hdf5NoIndex, "invalid frame");
    try {
        auto valueEnd = extent(mImpl->mValues);
        auto indexEnd = extent(mImpl->mIndices);
        auto byteEnd = extent(mImpl->mBytes);
        std::vector<Hdf5RecordInfo> metadata;
        metadata.reserve(views.size());
        for (const auto& view : views) {
            auto info = view.info;
            info.frame = mImpl->mFrame;
            info.value_offset = valueEnd;
            info.index_offset = indexEnd;
            info.byte_offset = byteEnd;
            if (info.type == Hdf5RecordType::VECTOR) {
                info.count = view.values.size();
                check(info.count <= hdf5NoIndex - valueEnd, "value offset overflow");
                extend(mImpl->mValues, valueEnd + info.count);
                transfer(mImpl->mValues,
                         H5T_NATIVE_DOUBLE,
                         valueEnd,
                         info.count,
                         const_cast<double*>(view.values.data()),
                         true);
                valueEnd += info.count;
            } else if (info.type == Hdf5RecordType::MATRIX) {
                check(info.count <= (hdf5NoIndex - indexEnd) / 2 &&
                          info.count <= hdf5NoIndex - valueEnd,
                      "matrix offset overflow");
                check(info.count == 0 || static_cast<bool>(view.matrix), "missing matrix producer");
                extend(mImpl->mValues, valueEnd + info.count);
                extend(mImpl->mIndices, indexEnd + (2 * info.count));
                constexpr std::size_t chunk = 8192;
                const auto capacity =
                    static_cast<std::size_t>((std::min)(info.count, std::uint64_t{chunk}));
                std::vector<double> data(capacity);
                std::vector<std::uint64_t> rows(capacity);
                std::vector<std::uint64_t> columns(capacity);
                for (std::uint64_t first = 0; first < info.count; first += chunk) {
                    const auto size = static_cast<std::size_t>(
                        (std::min)(info.count - first, std::uint64_t{chunk}));
                    view.matrix(std::span(data).first(size),
                                std::span(rows).first(size),
                                std::span(columns).first(size));
                    for (std::size_t entryIndex = 0; entryIndex < size; ++entryIndex) {
                        check(rows[entryIndex] < info.rows && columns[entryIndex] < info.columns,
                              "matrix coordinate exceeds shape");
                    }
                    transfer(mImpl->mValues,
                             H5T_NATIVE_DOUBLE,
                             valueEnd + first,
                             size,
                             data.data(),
                             true);
                    transfer(mImpl->mIndices,
                             H5T_NATIVE_UINT64,
                             indexEnd + first,
                             size,
                             rows.data(),
                             true);
                    transfer(mImpl->mIndices,
                             H5T_NATIVE_UINT64,
                             indexEnd + info.count + first,
                             size,
                             columns.data(),
                             true);
                }
                valueEnd += info.count;
                indexEnd += 2 * info.count;
            } else if (info.type == Hdf5RecordType::NAMES) {
                info.count = view.names.size();
                std::vector<std::uint64_t> offsets{0};
                std::string text;
                for (const auto& name : view.names) {
                    text += name;
                    offsets.push_back(text.size());
                }
                info.byte_count = text.size();
                check(info.byte_count <= hdf5NoIndex - byteEnd &&
                          offsets.size() <= hdf5NoIndex - indexEnd,
                      "name offset overflow");
                extend(mImpl->mBytes, byteEnd + info.byte_count);
                transfer(
                    mImpl->mBytes, H5T_NATIVE_UINT8, byteEnd, info.byte_count, text.data(), true);
                extend(mImpl->mIndices, indexEnd + offsets.size());
                transfer(mImpl->mIndices,
                         H5T_NATIVE_UINT64,
                         indexEnd,
                         offsets.size(),
                         offsets.data(),
                         true);
                byteEnd += info.byte_count;
                indexEnd += offsets.size();
            } else {
                check(false, "unsupported record type");
            }
            metadata.push_back(info);
        }
        check(metadata.size() <= hdf5NoIndex - mImpl->mCount, "record count overflow");
        const auto next = mImpl->mCount + metadata.size();
        extend(mImpl->mRecords, next);
        transfer(mImpl->mRecords,
                 mImpl->mNativeRecord,
                 mImpl->mCount,
                 metadata.size(),
                 metadata.data(),
                 true);
        check(H5Fflush(mImpl->mFile, H5F_SCOPE_GLOBAL) >= 0, "payload flush failed");
        check(
            H5Dwrite(mImpl->mCommitted, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &next) >=
                    0 &&
                H5Fflush(mImpl->mFile, H5F_SCOPE_GLOBAL) >= 0,
            "frame publication failed");
        mImpl->mCount = next;
        ++mImpl->mFrame;
    }
    catch (...) {
        mImpl->mFailed = true;
        throw;
    }
}

std::vector<Hdf5RecordInfo> Hdf5DataFile::readRecords(std::uint64_t first,
                                                      std::uint64_t count) const
{
    const std::scoped_lock lock(storageMutex());
    const ErrorScope errors;
    check(first <= mImpl->mCount, "invalid record selection");
    if (count == hdf5NoIndex) {
        count = mImpl->mCount - first;
    }
    range(first, count, mImpl->mCount);
    std::vector<Hdf5RecordInfo> result(checkedSize(count, sizeof(Hdf5RecordInfo)));
    transfer(mImpl->mRecords, mImpl->mNativeRecord, first, count, result.data(), false);
    for (const auto& info : result) {
        check(info.type == Hdf5RecordType::VECTOR || info.type == Hdf5RecordType::MATRIX ||
                  info.type == Hdf5RecordType::NAMES,
              "unsupported record type");
    }
    return result;
}
std::vector<double> Hdf5DataFile::readValues(const Hdf5RecordInfo& record,
                                             std::uint64_t first,
                                             std::uint64_t count) const
{
    const std::scoped_lock lock(storageMutex());
    const ErrorScope errors;
    check(record.type != Hdf5RecordType::NAMES, "names have no numeric values");
    if (count == hdf5NoIndex) {
        check(first <= record.count, "invalid value selection");
        count = record.count - first;
    }
    range(first, count, record.count);
    range(record.value_offset, record.count, extent(mImpl->mValues));
    std::vector<double> result(checkedSize(count, sizeof(double)));
    transfer(mImpl->mValues,
             H5T_NATIVE_DOUBLE,
             record.value_offset + first,
             count,
             result.data(),
             false);
    return result;
}
Hdf5SparseData Hdf5DataFile::readMatrix(const Hdf5RecordInfo& record) const
{
    Hdf5SparseData result;
    result.values = readValues(record);
    const std::scoped_lock lock(storageMutex());
    const ErrorScope errors;
    check(record.type == Hdf5RecordType::MATRIX && record.count <= hdf5NoIndex / 2,
          "invalid matrix record");
    range(record.index_offset, 2 * record.count, extent(mImpl->mIndices));
    result.row.resize(checkedSize(record.count, sizeof(std::uint64_t)));
    result.column.resize(result.row.size());
    transfer(mImpl->mIndices,
             H5T_NATIVE_UINT64,
             record.index_offset,
             record.count,
             result.row.data(),
             false);
    transfer(mImpl->mIndices,
             H5T_NATIVE_UINT64,
             record.index_offset + record.count,
             record.count,
             result.column.data(),
             false);
    for (std::size_t entryIndex = 0; entryIndex < result.row.size(); ++entryIndex) {
        check(result.row[entryIndex] < record.rows && result.column[entryIndex] < record.columns,
              "matrix coordinate exceeds shape");
    }
    return result;
}
std::vector<std::string> Hdf5DataFile::readNames(const Hdf5RecordInfo& record) const
{
    const std::scoped_lock lock(storageMutex());
    const ErrorScope errors;
    check(record.type == Hdf5RecordType::NAMES && record.count != hdf5NoIndex,
          "invalid names record");
    range(record.index_offset, record.count + 1, extent(mImpl->mIndices));
    range(record.byte_offset, record.byte_count, extent(mImpl->mBytes));
    std::vector<std::uint64_t> offsets(checkedSize(record.count + 1, sizeof(std::uint64_t)));
    transfer(mImpl->mIndices,
             H5T_NATIVE_UINT64,
             record.index_offset,
             offsets.size(),
             offsets.data(),
             false);
    check(offsets.front() == 0 && offsets.back() == record.byte_count &&
              std::is_sorted(offsets.begin(), offsets.end()),
          "invalid name offsets");
    std::string text(checkedSize(record.byte_count, 1), '\0');
    transfer(
        mImpl->mBytes, H5T_NATIVE_UINT8, record.byte_offset, record.byte_count, text.data(), false);
    std::vector<std::string> result;
    result.reserve(checkedSize(record.count, sizeof(std::string)));
    for (std::size_t entryIndex = 1; entryIndex < offsets.size(); ++entryIndex) {
        result.emplace_back(
            text.substr(static_cast<std::size_t>(offsets[entryIndex - 1]),
                        static_cast<std::size_t>(offsets[entryIndex] - offsets[entryIndex - 1])));
    }
    return result;
}
}  // namespace utilities
