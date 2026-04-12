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
#include <array>

/** @brief class implementation translation for another matrixData object
 most functions are just simple forwarding to the underlying matrixData object
except the assign and at operator which basically means the matrixData can interact with a small
subset of a bigger matrixData object though rowIndex, colIndex, and val will still return the
original values.  The intent of this class is not to replace the interactions with another it is to
act as a filter in cases where elements need to be added but the row needs a translation,  using it
outside that purpose could lead to issues
*/
template<int CT, class ValueT = double>
class matrixDataTranslate: public matrixDataContainer<ValueT> {
  private:
    std::array<index_t, CT> Trow;  //!< the vector of translations
  public:
    /** @brief constructor
     */
    matrixDataTranslate() { Trow.fill(kNullLocation); }
    explicit matrixDataTranslate(matrixData<ValueT>& input): matrixDataContainer<ValueT>(input)
    {
        Trow.fill(kNullLocation);
    }
    inline bool isValidRow(index_t row) const
    {
#ifdef UNSIGNED_INDEXING
        return ((row < CT) && (Trow[row] < matrixData<ValueT>::rowLimit()));
#else
        return ((row < CT) && (row >= 0) && (Trow[row] < matrixData<ValueT>::rowLimit()));
#endif
    }
    void assign(index_t row, index_t col, ValueT num) override
    {
        // for this to work the assignment must be from a small number to some other index
        // and we do automatic checking of the translation and if it isn't valid don't do the
        // assignment
        if (isValidRow(row)) {
            matrixDataContainer<ValueT>::md->assign(Trow[row], col, num);
        }
    };

    ValueT at(index_t rowN, index_t colN) const override
    {
        if (isValidRow(rowN)) {
            return matrixDataContainer<ValueT>::md->at(Trow[rowN], colN);
        }
        return ValueT(0);
    };

    /** set the translation array
    @param[in] input  the input row to translate
    @param[in] output the rowIndex to that input should be translated to
    */
    void setTranslation(index_t input, index_t output)
    {
        if ((input < CT) && (input >= 0)) {
            Trow[input] = output;
        }
    }
};
