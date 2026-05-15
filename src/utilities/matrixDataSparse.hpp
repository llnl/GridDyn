/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "matrixData.hpp"
#include "matrixDataOrdering.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

/**
 * class for storing data from the Jacobian computation
 */

/** @brief class implementing an expandable sparse matrix geared for Jacobian entries*/
template<class ValueT = double>
class matrixDataSparse: public matrixData<ValueT> {
  public:
    /** @brief constructor
        @param[in] startCount  the number of elements to allocate space for initially
    */
    explicit matrixDataSparse(index_t startCount = 50) { data_.reserve(startCount); }
    /**
     * function to clear the data
     */
    void clear() override { data_.clear(); }
    void assign(index_t row, index_t col, ValueT num) override;

    /**
     * @brief reserve space for the count of the Jacobian elements
     * @param[in] reserveSize the amount of space to reserve
     */
    void reserve(count_t reserveSize) override { data_.reserve(reserveSize); }
    count_t size() const override { return static_cast<count_t>(data_.size()); }
    count_t capacity() const override { return static_cast<count_t>(data_.capacity()); }
    /**
     * sort the index based first on column number then column number
     */
    void sortIndex(SparseOrdering ordering = SparseOrdering::ROW_ORDERED);

    /**
     * @brief compact the index merging values with the same row and
     * column number together
     */
    void compact() override;

    matrixElement<ValueT> element(index_t N) const override { return data_[N]; }
    auto begin() const noexcept { return data_.cbegin(); }
    auto end() const noexcept { return data_.end(); }
    void start() override { cptr_ = data_.cbegin(); }
    matrixElement<ValueT> next() override
    {
        matrixElement<ValueT> tp = *cptr_;
        ++cptr_;
        return tp;
    }

    bool moreData() override { return (cptr_ != data_.cend()); }
    /**
     * @brief get the number nonzero of elements in each row
     * @return a vector of the column counts
     */
    std::vector<count_t> columnCount();

    /** @brief check if the sparse array is sorted
        @return bool indicating sorted status
    */
    bool isSorted() const { return (sortCount_ == static_cast<count_t>(data_.size())); }
    ValueT at(index_t rowN, index_t colN) const override;

    /** @brief scale a subset of the elements
        @param[in] factor the scaling factor
        @param[in] startIndex the starting index
        @param[in] count the number of elements to scale
    */

    void scale(ValueT factor,
               index_t startIndex = 0,
               count_t count = (std::numeric_limits<count_t>::max)());

    /** @brief scale all the elements of a particular row
        @param[in] row the row to scale
        @param[in] factor the scaling factor
    */
    void scaleRow(index_t row, ValueT factor)
    {
        for (auto& res : data_) {
            if (res.row == row) {
                res.data *= factor;
            }
        }
    }

    /** @brief scale all the elements of a particular column
        @param[in] col the row to scale
        @param[in] factor the scaling factor
    */
    void scaleCol(index_t col, ValueT factor)
    {
        for (auto& res : data_) {
            if (res.col == col) {
                res.data *= factor;
            }
        }
    }

    /** @brief translate all the elements in a particular row to some other row
        @param[in] origRow the row to change
        @param[in] newRow the row to change origRow into
    */
    void translateRow(index_t origRow, index_t newRow)
    {
        for (auto& res : data_) {
            if (res.row == origRow) {
                res.row = newRow;
            }
        }
    }

    /** @brief translate all the elements in a particular column to some other column
        @param[in] origCol the column to change
        @param[in] newCol the column to change origCol into
    */
    void translateCol(index_t origCol, index_t newCol)
    {
        for (auto& res : data_) {
            if (res.col == origCol) {
                res.col = newCol;
            }
        }
    }

    using matrixData<ValueT>::copyTranslateRow;

    /** @brief translate all the elements in a particular row in a2 and translate row origRow to
       newRow
        @param[in] a2  the matrixDataSparse object to copy from
        @param[in] origRow the column to change
        @param[in] newRow the column to change origRow into
    */
    void copyTranslateRow(const matrixDataSparse<ValueT>& a2, index_t origRow, index_t newRow)
    {
        for (const auto& res : a2.data_) {
            if (res.row == origRow) {
                data_.emplace_back(newRow, res.col, res.data);
            }
        }
    }

    /** @brief translate all the elements in a particular column in a2 and translate column origCol
       to newCol
        @param[in] a2  the matrixDataSparse object to copy from
        @param[in] origCol the column to change
        @param[in] newCol the column to change origCol into
    */
    void copyTranslateCol(const matrixDataSparse<ValueT>& a2, index_t origCol, index_t newCol)
    {
        for (const auto& res : a2.data_) {
            if (res.col == origCol) {
                data_.emplace_back(res.row, newCol, res.data);
            }
        }
    }

    /** @brief copy all data from a2 to the array and translate
        origCol into a set of new indices and scale factors.

        This function is useful when the input to some object is a
        summation of other states, this function allows the
        translation into a large sparse data object.

        @param[in] a2  the matrixDataSparse object to copy from
        @param[in] origCol the column to change
        @param[in] newIndices a vector of indices to change
        @param[in] mult the scaler multiplier for each fo the new indices
    */
    void copyReplicate(const matrixDataSparse<ValueT>& a2,
                       index_t origCol,
                       std::vector<index_t> newIndices,
                       std::vector<ValueT> mult);

    /** @brief remove invalid rows or those given by the testrow
        @param[in] rowTest the row index to remove*/
    void filter(index_t rowTest = kNullLocation);

    void cascade(matrixDataSparse<ValueT>& a2, index_t element);

    using matrixData<ValueT>::merge;
    void merge(matrixDataSparse<ValueT>& a2)
    {
        data_.insert(data_.end(), a2.data_.begin(), a2.data_.end());
    }
    void transpose()
    {
        for (auto& dataElement : data_) {
            std::swap(dataElement.col, dataElement.row);
            //    int t1 = std::get<adCol>(data_[kk]);
            //    std::get<adCol>(data_[kk]) = std::get<adRow>(data_[kk]);
            //    std::get<adRow>(data_[kk]) = t1;
        }
    }
    void diagMultiply(std::vector<ValueT> diag)
    {
        for (auto& dataElement : data_) {
            dataElement.data *= diag[dataElement.col];
        }
    }

    std::vector<ValueT> vectorMult(std::vector<ValueT> V);

  private:
    count_t sortCount_ = 0;  //!< count of the last sort operation
    /** @brief the vector of tuples containing the data */
    std::vector<matrixElement<ValueT>> data_;

    decltype(data_.cbegin()) cptr_;  //!< ptr to the beginning of the sequence
};

template<class ValueT>
std::vector<index_t> findMissing(matrixDataSparse<ValueT>& md);

template<class ValueT>
std::vector<std::vector<index_t>> findRank(matrixDataSparse<ValueT>& md);
