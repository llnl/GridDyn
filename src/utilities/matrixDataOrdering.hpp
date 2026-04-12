/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#ifndef _MATRIX_DATA_ORDERING_H_
#define _MATRIX_DATA_ORDERING_H_
#pragma once

#include <utility>

/** @brief enumeration specifying sparse ordering patterns*/
enum class sparse_ordering {
    row_ordered = 0,
    column_ordered = 1,
};

// default to row major ordering
/** class to reorder row and column into primary and secondary indices
 */
template<class Y, sparse_ordering M>
class keyOrder {
  public:
    static Y primary(Y rowIndex, Y /*colIndex*/) { return rowIndex; }
    static Y secondary(Y /*rowIndex*/, Y colIndex) { return colIndex; }
    static std::pair<Y, Y> order(Y row, Y col) { return std::make_pair(row, col); }
};

template<class Y>
class keyOrder<Y, sparse_ordering::column_ordered> {
  public:
    static Y primary(Y /*rowIndex*/, Y colIndex) { return colIndex; }
    static Y secondary(Y rowIndex, Y /*colIndex*/) { return rowIndex; }
    static std::pair<Y, Y> order(Y row, Y col) { return std::make_pair(col, row); }
};

#endif
