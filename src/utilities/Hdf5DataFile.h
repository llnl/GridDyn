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
#include <string_view>
#include <vector>

namespace utilities {
/** Sentinel for absent metadata indices or for reading to the end of a range. */
inline constexpr std::uint64_t hdf5NoIndex = (std::numeric_limits<std::uint64_t>::max)();
/** Payload kinds stored in the on-disk record index. */
enum class Hdf5RecordType : std::uint32_t {
    VECTOR = 0,  //!< Contiguous doubles.
    MATRIX = 1,  //!< Coordinate-form sparse matrix.
    NAMES = 2,  //!< Ordered strings with stored byte offsets.
    BYTES = 3  //!< Opaque byte sequence.
};

/** Domain-neutral capture metadata. Domain adapters define source/code/flags/key. */
struct Hdf5RecordInfo {
    std::int64_t time_ns = 0;  //!< Producer time in exact nanoseconds.
    std::uint64_t frame = 0;  //!< File-assigned ID shared by the records of one append.
    std::uint64_t index = 0;  //!< Producer-supplied record index within its capture context.
    std::uint64_t key = hdf5NoIndex;  //!< Optional producer key such as a solver offset index.
    std::uint64_t paired_key = hdf5NoIndex;  //!< Key of a related payload in the same context.
    std::uint64_t parent_index = hdf5NoIndex;  //!< Optional producer reference to a parent record.
    std::uint64_t value_offset = 0;  //!< Start in the shared double dataset.
    std::uint64_t count = 0;  //!< Number of vector values, matrix entries or names.
    std::uint64_t index_offset = 0;  //!< Start of matrix coordinates or string offsets.
    std::uint64_t byte_offset = 0;  //!< Start in the shared byte dataset.
    std::uint64_t byte_count = 0;  //!< Length of the byte or string payload.
    std::uint64_t rows = 0;  //!< Stored matrix row count.
    std::uint64_t columns = 0;  //!< Stored matrix column count.
    double cj = std::numeric_limits<double>::quiet_NaN();  //!< Optional Jacobian coefficient.
    std::uint32_t source = 0;  //!< Producer-defined capture origin.
    Hdf5RecordType type = Hdf5RecordType::VECTOR;  //!< Payload encoding.
    std::uint32_t code = 0;  //!< Producer-defined role within the capture.
    std::uint32_t flags = 0;  //!< Producer-defined flags such as solver-mode properties.
    std::uint32_t approx = 0;  //!< Producer approximation bits, when supplied.
};

/** Borrowed arrays are consumed synchronously by append(). Sparse producers fill bounded chunks. */
struct Hdf5RecordView {
    Hdf5RecordInfo info;  //!< Record kind and metadata. Storage assigns frame and offsets.
    std::span<const double> values;  //!< First contiguous range of a vector payload.
    std::span<const std::string> names;  //!< Ordered strings for a names record.
    std::string_view bytes;  //!< Raw payload for a bytes record.
    /** Fill the next equally sized chunks of values, zero-based rows and zero-based columns. */
    std::function<void(std::span<double>, std::span<std::uint64_t>, std::span<std::uint64_t>)>
        matrix;
    /** Additional contiguous ranges for a VECTOR, written after values. */
    std::span<const std::span<const double>> valueSegments;
};

/** Sparse entries in stored order. The corresponding record holds the matrix shape. */
struct Hdf5SparseData {
    std::vector<std::uint64_t> row;  //!< Zero-based row coordinate of each entry.
    std::vector<std::uint64_t> column;  //!< Zero-based column coordinate of each entry.
    std::vector<double> values;  //!< Values in the same order as the coordinates.
};

/** One shared storage implementation for vectors, sparse matrices and names.
 * All HDF5 calls owned by this class are serialized, including destruction.
 * append() publishes a complete frame after its payload and metadata are flushed.
 * This is a logical completion protocol, not a crash-durable transaction.
 * Throws std::runtime_error on unsupported files, structural errors and failed I/O.
 */
class Hdf5DataFile {
  public:
    /** How to open a file and interact with an active in-process writer. */
    enum class OpenMode {
        READ_ONLY,  //!< Open an existing file for reads.
        APPEND,  //!< Continue a compatible file or create it if absent.
        TRUNCATE,  //!< Replace the file. An active writer prevents truncation.
        CREATE_OR_JOIN  //!< Join an active writer or start a new file at this path.
    };
    /** Open path with the requested access. Compression selects deflate for new datasets. */
    explicit Hdf5DataFile(const std::string& path, OpenMode mode, unsigned compression = 0);
    ~Hdf5DataFile();
    Hdf5DataFile(const Hdf5DataFile&) = delete;
    Hdf5DataFile& operator=(const Hdf5DataFile&) = delete;

    /** Write a nonempty frame and publish its records together. */
    void append(std::span<const Hdf5RecordView> views);
    /** Number of published record rows. */
    std::uint64_t recordCount() const;
    /** Read published metadata starting at a zero-based row. The default count reads to the end. */
    std::vector<Hdf5RecordInfo> readRecords(std::uint64_t first = 0,
                                            std::uint64_t count = hdf5NoIndex) const;
    /** Read doubles starting at an offset within a vector or matrix payload. */
    std::vector<double> readValues(const Hdf5RecordInfo& record,
                                   std::uint64_t first = 0,
                                   std::uint64_t count = hdf5NoIndex) const;
    /** Read a matrix's entries and validate their coordinates against the stored shape. */
    Hdf5SparseData readMatrix(const Hdf5RecordInfo& record) const;
    /** Read a names payload in the order it was stored. */
    std::vector<std::string> readNames(const Hdf5RecordInfo& record) const;
    /** Read a bytes payload, preserving embedded NULs. */
    std::string readBytes(const Hdf5RecordInfo& record) const;

  private:
    class Impl;
    std::shared_ptr<Impl> mImpl;
    bool mReadOnly = true;
};
}  // namespace utilities
