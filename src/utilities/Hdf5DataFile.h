/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace utilities {
inline constexpr std::uint64_t hdf5NoIndex = (std::numeric_limits<std::uint64_t>::max)();
enum class Hdf5RecordType : std::uint32_t { VECTOR = 0, MATRIX = 1, NAMES = 2 };

/** Domain-neutral capture metadata. Domain adapters define source/code/flags/key. */
struct Hdf5RecordInfo {
    std::int64_t time_ns = 0;
    std::uint64_t frame = 0;
    std::uint64_t index = 0;
    std::uint64_t key = hdf5NoIndex;
    std::uint64_t paired_key = hdf5NoIndex;
    std::uint64_t parent_index = hdf5NoIndex;
    std::uint64_t value_offset = 0;
    std::uint64_t count = 0;
    std::uint64_t index_offset = 0;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_count = 0;
    std::uint64_t rows = 0;
    std::uint64_t columns = 0;
    double cj = std::numeric_limits<double>::quiet_NaN();
    std::uint32_t source = 0;
    Hdf5RecordType type = Hdf5RecordType::VECTOR;
    std::uint32_t code = 0;
    std::uint32_t flags = 0;
    std::uint32_t approx = 0;
};

/** Borrowed arrays are consumed synchronously by append(). Sparse producers fill bounded chunks. */
struct Hdf5RecordView {
    Hdf5RecordInfo info;
    std::span<const double> values;
    std::span<const std::string> names;
    std::function<void(std::span<double>, std::span<std::uint64_t>, std::span<std::uint64_t>)>
        matrix;
};

struct Hdf5SparseData {
    std::vector<std::uint64_t> row;
    std::vector<std::uint64_t> column;
    std::vector<double> values;
};

/** One shared storage implementation for vectors, sparse matrices and names.
 * All HDF5 calls owned by this class are serialized, including destruction.
 * append() publishes a complete frame after its payload and metadata are flushed.
 * This is a logical completion protocol, not a crash-durable transaction.
 * Throws std::runtime_error on unsupported files, structural errors and failed I/O.
 */
class Hdf5DataFile {
  public:
    enum class OpenMode { READ_ONLY, APPEND, TRUNCATE, CREATE_OR_JOIN };
    explicit Hdf5DataFile(const std::string& path, OpenMode mode, unsigned compression = 0);
    ~Hdf5DataFile();
    Hdf5DataFile(const Hdf5DataFile&) = delete;
    Hdf5DataFile& operator=(const Hdf5DataFile&) = delete;

    void append(std::span<const Hdf5RecordView> views);
    std::uint64_t recordCount() const;
    std::vector<Hdf5RecordInfo> readRecords(std::uint64_t first = 0,
                                            std::uint64_t count = hdf5NoIndex) const;
    std::vector<double> readValues(const Hdf5RecordInfo& record,
                                   std::uint64_t first = 0,
                                   std::uint64_t count = hdf5NoIndex) const;
    Hdf5SparseData readMatrix(const Hdf5RecordInfo& record) const;
    std::vector<std::string> readNames(const Hdf5RecordInfo& record) const;

  private:
    class Impl;
    std::shared_ptr<Impl> mImpl;
    bool mReadOnly = true;
};
}  // namespace utilities
