/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Hdf5DataFile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <hdf5.h>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

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
        hid_t id = -1;
        herr_t (*close)(hid_t) = nullptr;
        Handle() = default;
        Handle(hid_t value, herr_t (*closer)(hid_t)): id(value), close(closer)
        {
            check(id >= 0, "unable to create/open an HDF5 object");
        }
        ~Handle()
        {
            if (id >= 0) {
                close(id);
            }
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& other) noexcept: id(std::exchange(other.id, -1)), close(other.close) {}
        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other) {
                if (id >= 0) {
                    close(id);
                }
                id = std::exchange(other.id, -1);
                close = other.close;
            }
            return *this;
        }
        operator hid_t() const { return id; }
    };
    // Suppress only this thread's temporary error printer; retain the caller's policy.
    class ErrorScope {
        H5E_auto2_t printer = nullptr;
        void* data = nullptr;

      public:
        ErrorScope()
        {
            H5Eget_auto2(H5E_DEFAULT, &printer, &data);
            H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
        }
        ~ErrorScope() { H5Eset_auto2(H5E_DEFAULT, printer, data); }
    };
    std::uint64_t extent(hid_t dataset)
    {
        Handle space(H5Dget_space(dataset), H5Sclose);
        hsize_t size = 0;
        check(H5Sget_simple_extent_ndims(space) == 1 &&
                  H5Sget_simple_extent_dims(space, &size, nullptr) >= 0,
              "expected a one-dimensional dataset");
        return size;
    }
    std::size_t checkedSize(std::uint64_t count, std::size_t elementSize)
    {
        check(count <= (std::numeric_limits<std::size_t>::max)() / elementSize,
              "array is too large");
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
        Handle fileSpace(H5Dget_space(dataset), H5Sclose);
        hsize_t start = first, size = count;
        check(H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET, &start, nullptr, &size, nullptr) >= 0,
              "unable to select dataset range");
        Handle memorySpace(H5Screate_simple(1, &size, nullptr), H5Sclose);
        check((write ? H5Dwrite(dataset, type, memorySpace, fileSpace, H5P_DEFAULT, buffer) :
                       H5Dread(dataset, type, memorySpace, fileSpace, H5P_DEFAULT, buffer)) >= 0,
              "dataset I/O failed");
    }
    void extend(hid_t dataset, std::uint64_t size)
    {
        hsize_t end = size;
        check(H5Dset_extent(dataset, &end) >= 0, "unable to extend dataset");
    }
    Handle recordType(bool native)
    {
        Handle type(H5Tcreate(H5T_COMPOUND, sizeof(Hdf5RecordInfo)), H5Tclose);
#define FIELD(name, disk, memory)                                                                  \
    check(H5Tinsert(type, #name, offsetof(Hdf5RecordInfo, name), native ? memory : disk) >= 0,     \
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
        const hsize_t zero = 0, unlimited = H5S_UNLIMITED;
        Handle space(H5Screate_simple(1, &zero, &unlimited), H5Sclose);
        Handle properties(H5Pcreate(H5P_DATASET_CREATE), H5Pclose);
        check(H5Pset_chunk(properties, 1, &chunk) >= 0, "unable to configure chunks");
        if (compression > 0) {
            check(H5Pset_shuffle(properties) >= 0 && H5Pset_deflate(properties, compression) >= 0,
                  "unable to configure lossless compression");
        }
        return Handle(H5Dcreate2(file, name, type, space, H5P_DEFAULT, properties, H5P_DEFAULT),
                      H5Dclose);
    }
    void scalarAttribute(hid_t file, const char* name, std::uint32_t value)
    {
        Handle space(H5Screate(H5S_SCALAR), H5Sclose);
        Handle attr(H5Acreate2(file, name, H5T_STD_U32LE, space, H5P_DEFAULT, H5P_DEFAULT),
                    H5Aclose);
        check(H5Awrite(attr, H5T_NATIVE_UINT32, &value) >= 0, "attribute write failed");
    }
}  // namespace

class Hdf5DataFile::Impl {
  public:
    Handle file, records, values, indices, bytes, committed, nativeRecord;
    std::uint64_t count = 0, frame = 0;
    bool writable = false, failed = false;
    Impl(const std::string& path, OpenMode mode, unsigned compression):
        nativeRecord(recordType(true))
    {
        check(compression <= 9, "compression must be between 0 and 9");
        writable = mode != OpenMode::READ_ONLY;
        const bool create = mode == OpenMode::TRUNCATE ||
            (mode == OpenMode::APPEND && !std::filesystem::exists(path));
        if (create) {
            Handle access(H5Pcreate(H5P_FILE_ACCESS), H5Pclose);
            check(H5Pset_libver_bounds(access, H5F_LIBVER_V18, H5F_LIBVER_V18) >= 0,
                  "unable to set file feature bounds");
            file = Handle(H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, access), H5Fclose);
            Handle textType(H5Tcopy(H5T_C_S1), H5Tclose);
            constexpr char format[] = "griddyn-data";
            check(H5Tset_size(textType, sizeof(format)) >= 0, "string type failed");
            Handle scalar(H5Screate(H5S_SCALAR), H5Sclose);
            Handle attr(H5Acreate2(file, "format", textType, scalar, H5P_DEFAULT, H5P_DEFAULT),
                        H5Aclose);
            check(H5Awrite(attr, textType, format) >= 0, "format attribute write failed");
            scalarAttribute(file, "schema_version", 1);
            auto diskRecord = recordType(false);
            records = createStream(file, "/records", diskRecord, 128, 0);
            values = createStream(file, "/values", H5T_IEEE_F64LE, 8192, compression);
            indices = createStream(file, "/indices", H5T_STD_U64LE, 8192, compression);
            bytes = createStream(file, "/bytes", H5T_STD_U8LE, 65536, compression);
            committed = Handle(H5Dcreate2(file,
                                          "/committed_records",
                                          H5T_STD_U64LE,
                                          scalar,
                                          H5P_DEFAULT,
                                          H5P_DEFAULT,
                                          H5P_DEFAULT),
                               H5Dclose);
            check(H5Dwrite(committed, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &count) >=
                      0,
                  "completion initialization failed");
        } else {
            file =
                Handle(H5Fopen(path.c_str(), writable ? H5F_ACC_RDWR : H5F_ACC_RDONLY, H5P_DEFAULT),
                       H5Fclose);
            Handle attr(H5Aopen(file, "format", H5P_DEFAULT), H5Aclose);
            Handle formatSpace(H5Aget_space(attr), H5Sclose);
            check(H5Sget_simple_extent_npoints(formatSpace) == 1, "invalid format attribute shape");
            Handle textType(H5Aget_type(attr), H5Tclose);
            check(H5Tget_class(textType) == H5T_STRING && H5Tis_variable_str(textType) == 0 &&
                      H5Tget_size(textType) == 13,
                  "unsupported format");
            std::array<char, 13> format{};
            check(H5Aread(attr, textType, format.data()) >= 0 &&
                      std::string_view(format.data(), 12) == "griddyn-data",
                  "unsupported format");
            Handle versionAttr(H5Aopen(file, "schema_version", H5P_DEFAULT), H5Aclose);
            Handle versionSpace(H5Aget_space(versionAttr), H5Sclose);
            check(H5Sget_simple_extent_npoints(versionSpace) == 1,
                  "invalid version attribute shape");
            std::uint32_t version = 0;
            check(H5Aread(versionAttr, H5T_NATIVE_UINT32, &version) >= 0 && version == 1,
                  "unsupported schema version");
            records = Handle(H5Dopen2(file, "/records", H5P_DEFAULT), H5Dclose);
            values = Handle(H5Dopen2(file, "/values", H5P_DEFAULT), H5Dclose);
            indices = Handle(H5Dopen2(file, "/indices", H5P_DEFAULT), H5Dclose);
            bytes = Handle(H5Dopen2(file, "/bytes", H5P_DEFAULT), H5Dclose);
            committed = Handle(H5Dopen2(file, "/committed_records", H5P_DEFAULT), H5Dclose);
            const auto requireType = [](hid_t dataset, hid_t expected) {
                Handle actual(H5Dget_type(dataset), H5Tclose);
                check(H5Tequal(actual, expected) > 0, "unsupported dataset datatype");
            };
            auto diskRecord = recordType(false);
            requireType(records, diskRecord);
            requireType(values, H5T_IEEE_F64LE);
            requireType(indices, H5T_STD_U64LE);
            requireType(bytes, H5T_STD_U8LE);
            requireType(committed, H5T_STD_U64LE);
            Handle scalar(H5Dget_space(committed), H5Sclose);
            check(H5Sget_simple_extent_type(scalar) == H5S_SCALAR, "invalid completion dataset");
            check(H5Dread(committed, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &count) >= 0,
                  "unable to read completion count");
            const auto stored = extent(records);
            check(count <= stored && (!writable || count == stored), "incomplete record index");
            if (count > 0) {
                Hdf5RecordInfo last;
                transfer(records, nativeRecord, count - 1, 1, &last, false);
                check(last.frame != hdf5NoIndex, "frame number overflow");
                frame = last.frame + 1;
            }
        }
    }
};

Hdf5DataFile::Hdf5DataFile(const std::string& path, OpenMode mode, unsigned compression)
{
    const std::lock_guard lock(storageMutex());
    ErrorScope errors;
    readOnly = mode == OpenMode::READ_ONLY;
    if (!readOnly) {
        static std::unordered_map<std::string, std::weak_ptr<Impl>> writers;
        const auto key = std::filesystem::absolute(path).lexically_normal().string();
        std::erase_if(writers, [](const auto& entry) { return entry.second.expired(); });
        if (auto active = writers[key].lock()) {
            check(mode != OpenMode::TRUNCATE, "cannot truncate an active output session");
            impl = std::move(active);
            return;
        }
        impl = std::make_shared<Impl>(path,
                                      mode == OpenMode::CREATE_OR_JOIN ? OpenMode::TRUNCATE : mode,
                                      compression);
        writers[key] = impl;
    } else {
        impl = std::make_shared<Impl>(path, mode, compression);
    }
}
Hdf5DataFile::~Hdf5DataFile()
{
    const std::lock_guard lock(storageMutex());
    impl.reset();
}
std::uint64_t Hdf5DataFile::recordCount() const
{
    const std::lock_guard lock(storageMutex());
    return impl->count;
}

void Hdf5DataFile::append(std::span<const Hdf5RecordView> views)
{
    const std::lock_guard lock(storageMutex());
    ErrorScope errors;
    check(!readOnly && impl->writable && !impl->failed, "file is not writable");
    check(!views.empty() && impl->frame != hdf5NoIndex, "invalid frame");
    try {
        auto valueEnd = extent(impl->values), indexEnd = extent(impl->indices),
             byteEnd = extent(impl->bytes);
        std::vector<Hdf5RecordInfo> metadata;
        metadata.reserve(views.size());
        for (const auto& view : views) {
            auto info = view.info;
            info.frame = impl->frame;
            info.value_offset = valueEnd;
            info.index_offset = indexEnd;
            info.byte_offset = byteEnd;
            if (info.type == Hdf5RecordType::VECTOR) {
                info.count = view.values.size();
                check(info.count <= hdf5NoIndex - valueEnd, "value offset overflow");
                extend(impl->values, valueEnd + info.count);
                transfer(impl->values,
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
                extend(impl->values, valueEnd + info.count);
                extend(impl->indices, indexEnd + 2 * info.count);
                constexpr std::size_t chunk = 8192;
                const auto capacity =
                    static_cast<std::size_t>((std::min)(info.count, std::uint64_t{chunk}));
                std::vector<double> data(capacity);
                std::vector<std::uint64_t> rows(capacity), columns(capacity);
                for (std::uint64_t first = 0; first < info.count; first += chunk) {
                    const auto size = static_cast<std::size_t>(
                        (std::min)(info.count - first, std::uint64_t{chunk}));
                    view.matrix(std::span(data).first(size),
                                std::span(rows).first(size),
                                std::span(columns).first(size));
                    for (std::size_t i = 0; i < size; ++i) {
                        check(rows[i] < info.rows && columns[i] < info.columns,
                              "matrix coordinate exceeds shape");
                    }
                    transfer(
                        impl->values, H5T_NATIVE_DOUBLE, valueEnd + first, size, data.data(), true);
                    transfer(impl->indices,
                             H5T_NATIVE_UINT64,
                             indexEnd + first,
                             size,
                             rows.data(),
                             true);
                    transfer(impl->indices,
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
                extend(impl->bytes, byteEnd + info.byte_count);
                transfer(
                    impl->bytes, H5T_NATIVE_UINT8, byteEnd, info.byte_count, text.data(), true);
                extend(impl->indices, indexEnd + offsets.size());
                transfer(impl->indices,
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
        check(metadata.size() <= hdf5NoIndex - impl->count, "record count overflow");
        const auto next = impl->count + metadata.size();
        extend(impl->records, next);
        transfer(
            impl->records, impl->nativeRecord, impl->count, metadata.size(), metadata.data(), true);
        check(H5Fflush(impl->file, H5F_SCOPE_GLOBAL) >= 0, "payload flush failed");
        check(H5Dwrite(impl->committed, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &next) >=
                      0 &&
                  H5Fflush(impl->file, H5F_SCOPE_GLOBAL) >= 0,
              "frame publication failed");
        impl->count = next;
        ++impl->frame;
    }
    catch (...) {
        impl->failed = true;
        throw;
    }
}

std::vector<Hdf5RecordInfo> Hdf5DataFile::readRecords(std::uint64_t first,
                                                      std::uint64_t count) const
{
    const std::lock_guard lock(storageMutex());
    ErrorScope errors;
    check(first <= impl->count, "invalid record selection");
    if (count == hdf5NoIndex) {
        count = impl->count - first;
    }
    range(first, count, impl->count);
    std::vector<Hdf5RecordInfo> result(checkedSize(count, sizeof(Hdf5RecordInfo)));
    transfer(impl->records, impl->nativeRecord, first, count, result.data(), false);
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
    const std::lock_guard lock(storageMutex());
    ErrorScope errors;
    check(record.type != Hdf5RecordType::NAMES, "names have no numeric values");
    if (count == hdf5NoIndex) {
        check(first <= record.count, "invalid value selection");
        count = record.count - first;
    }
    range(first, count, record.count);
    range(record.value_offset, record.count, extent(impl->values));
    std::vector<double> result(checkedSize(count, sizeof(double)));
    transfer(
        impl->values, H5T_NATIVE_DOUBLE, record.value_offset + first, count, result.data(), false);
    return result;
}
Hdf5SparseData Hdf5DataFile::readMatrix(const Hdf5RecordInfo& record) const
{
    Hdf5SparseData result;
    result.values = readValues(record);
    const std::lock_guard lock(storageMutex());
    ErrorScope errors;
    check(record.type == Hdf5RecordType::MATRIX && record.count <= hdf5NoIndex / 2,
          "invalid matrix record");
    range(record.index_offset, 2 * record.count, extent(impl->indices));
    result.row.resize(checkedSize(record.count, sizeof(std::uint64_t)));
    result.column.resize(result.row.size());
    transfer(impl->indices,
             H5T_NATIVE_UINT64,
             record.index_offset,
             record.count,
             result.row.data(),
             false);
    transfer(impl->indices,
             H5T_NATIVE_UINT64,
             record.index_offset + record.count,
             record.count,
             result.column.data(),
             false);
    for (std::size_t i = 0; i < result.row.size(); ++i) {
        check(result.row[i] < record.rows && result.column[i] < record.columns,
              "matrix coordinate exceeds shape");
    }
    return result;
}
std::vector<std::string> Hdf5DataFile::readNames(const Hdf5RecordInfo& record) const
{
    const std::lock_guard lock(storageMutex());
    ErrorScope errors;
    check(record.type == Hdf5RecordType::NAMES && record.count != hdf5NoIndex,
          "invalid names record");
    range(record.index_offset, record.count + 1, extent(impl->indices));
    range(record.byte_offset, record.byte_count, extent(impl->bytes));
    std::vector<std::uint64_t> offsets(checkedSize(record.count + 1, sizeof(std::uint64_t)));
    transfer(impl->indices,
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
        impl->bytes, H5T_NATIVE_UINT8, record.byte_offset, record.byte_count, text.data(), false);
    std::vector<std::string> result;
    result.reserve(checkedSize(record.count, sizeof(std::string)));
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        result.emplace_back(text.substr(static_cast<std::size_t>(offsets[i - 1]),
                                        static_cast<std::size_t>(offsets[i] - offsets[i - 1])));
    }
    return result;
}
}  // namespace utilities
