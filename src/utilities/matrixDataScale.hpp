/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#pragma once

#include "matrixDataContainer.hpp"

/** @brief class implementation for a scaling array data
 all data is multiplied by a factor before being sent to the underlying matrixData object
*/
template<class ValueT = double, class ScaleT = ValueT>
class matrixDataScale: public matrixDataContainer<ValueT> {
  private:
    ScaleT scalingFactor_;

  public:
    /** @brief constructor
     */
    matrixDataScale(matrixData<ValueT>& input, ScaleT scaleFactor):
        matrixDataContainer<ValueT>(input), scalingFactor_(scaleFactor)
    {
    }

    void assign(index_t row, index_t col, ValueT num) override
    {
        matrixDataContainer<ValueT>::md->assign(row, col, num * scalingFactor_);
    }
    /** @brief set the scale factor for the array
    @param[in] scaleFactor  the input row to translate
    */
    void setScale(ScaleT scaleFactor) { scalingFactor_ = scaleFactor; }
};
