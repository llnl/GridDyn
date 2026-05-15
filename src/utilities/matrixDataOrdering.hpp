/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <utility>

/** @brief enumeration specifying sparse ordering patterns*/
enum class SparseOrdering {
    ROW_ORDERED = 0,
    COLUMN_ORDERED = 1,
};

// default to row major ordering
/** class to reorder row and column into primary and secondary indices
 */
template<class Y, SparseOrdering M>
class keyOrder {
  public:
    static Y primary(Y rowIndex, Y /*colIndex*/) { return rowIndex; }
    static Y secondary(Y /*rowIndex*/, Y colIndex) { return colIndex; }
    static std::pair<Y, Y> order(Y row, Y col) { return std::make_pair(row, col); }
};

template<class Y>
class keyOrder<Y, SparseOrdering::COLUMN_ORDERED> {
  public:
    static Y primary(Y /*rowIndex*/, Y colIndex) { return colIndex; }
    static Y secondary(Y rowIndex, Y /*colIndex*/) { return rowIndex; }
    static std::pair<Y, Y> order(Y row, Y col) { return std::make_pair(col, row); }
};
