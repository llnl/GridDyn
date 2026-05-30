/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "utilities/MatrixData.hpp"

/** @brief intermediate class for implementing a containing matrix data
@details class is abstract and cannot be instantiated on its own meant to help some
other classes that do things to the input before transmitting it.
uses a pointer so it can be reassigned later
*/
template<class ValueT = double>
class MatrixDataContainer: public MatrixData<ValueT> {
  public:
    MatrixData<ValueT>* md;  //!< the matrix this class contains
  public:
    MatrixDataContainer() = default;
    /** @brief constructor
     */
    explicit MatrixDataContainer(MatrixData<ValueT>& input)
    {
        MatrixDataContainer::setArray(&input);
    };
    void clear() override { md->clear(); };
    void assign(index_t row, index_t col, ValueT num) override = 0;

    count_t size() const override { return md->size(); };
    void reserve(count_t maxNonZeros) override { md->reserve(maxNonZeros); }
    count_t capacity() const override { return md->capacity(); };
    MatrixElement<ValueT> element(index_t N) const override { return md->element(N); }
    void compact() override { md->compact(); }
    void start() override { md->start(); }
    MatrixElement<ValueT> next() override { return md->next(); }
    bool moreData() override { return md->moreData(); }
    ValueT at(index_t rowN, index_t colN) const override { return md->at(rowN, colN); };
    /** set the MatrixData object to translate to
    @param[in] newAd  the new MatrixData object
    */
    virtual void setArray(MatrixData<ValueT>* newAd)
    {
        md = newAd;
        MatrixData<ValueT>::setColLimit(md->colLimit());
        MatrixData<ValueT>::setRowLimit(md->rowLimit());
    }
    /** set the MatrixData object to translate to
    @param[in] newAd  the new MatrixData object
    */
    virtual void setArray(MatrixData<ValueT>& newAd) { setArray(&newAd); }
};
