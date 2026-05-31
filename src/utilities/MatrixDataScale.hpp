/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "MatrixDataContainer.hpp"

/** @brief class implementation for a scaling array data
 all data is multiplied by a factor before being sent to the underlying MatrixData object
*/
template<class ValueT = double, class ScaleT = ValueT>
class MatrixDataScale: public MatrixDataContainer<ValueT> {
  private:
    ScaleT scalingFactor_;

  public:
    /** @brief constructor
     */
    MatrixDataScale(MatrixData<ValueT>& input, ScaleT scaleFactor):
        MatrixDataContainer<ValueT>(input), scalingFactor_(scaleFactor)
    {
    }

    void assign(index_t row, index_t col, ValueT num) override
    {
        MatrixDataContainer<ValueT>::md->assign(row, col, num * scalingFactor_);
    }
    /** @brief set the scale factor for the array
    @param[in] scaleFactor  the input row to translate
    */
    void setScale(ScaleT scaleFactor) { scalingFactor_ = scaleFactor; }
};
