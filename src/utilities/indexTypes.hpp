#pragma once

/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National L */

#include <cstdint>
#include <limits>

#ifdef UNSIGNED_INDEXING
#    ifdef ENABLE_64_BIT_INDEXING
using index_t = std::uint64_t;
#    else
using index_t = std::uint32_t;
#    endif
constexpr count_t kInvalidCount(std::numeric_limits<index_t>::max());
#else
#    ifdef ENABLE_64_BIT_INDEXING
using index_t = std::int64_t;
#    else
using index_t = std::int32_t;
#    endif
// count_t is used for semantic differences it should be the same type as index_t;

using count_t = index_t;
constexpr count_t kInvalidCount(std::numeric_limits<index_t>::min());

#endif
constexpr index_t kNullLocation(std::numeric_limits<index_t>::max());
constexpr index_t kInvalidLocation(std::numeric_limits<index_t>::max() - 1);
constexpr index_t kIndexMax(std::numeric_limits<index_t>::max());
constexpr count_t kCountMax(std::numeric_limits<count_t>::max());

constexpr index_t operator""_ind(unsigned long long val)  // NOLINT
{
    return index_t(val);
}
constexpr count_t operator""_cnt(unsigned long long val)  // NOLINT
{
    return count_t(val);
}
