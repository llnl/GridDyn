/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "utilities/MatrixData.hpp"
#include <array>

/** @brief class implementing a dense matrix geared for Jacobian entries
 this MatrixData structure is intended to be for small dense matrices with a required fast value
lookup and possibly lots of duplicate entries
*/
template<count_t R, count_t C, class ValueT = double>
class MatrixDataCompact: public MatrixData<ValueT> {
  private:
    std::array<ValueT, R * C> dVec{};  //!< the array containing the data
    index_t Rctr = 0;
    index_t Cctr = 0;

  public:
    /** @brief compact constructor
     */
    MatrixDataCompact(): MatrixData<ValueT>(R, C) {}

    virtual void clear() override { dVec.fill(0); }
    virtual void assign(index_t row, index_t col, ValueT num) override
    {
        // in column major order
        dVec[col * R + row] += num;
    }

    virtual count_t size() const override { return R * C; }
    virtual count_t capacity() const override { return R * C; }
    virtual void limitUpdate(index_t newRowLimit, index_t newColLimit) final override
    {  // the row and col limits cannot change
        if (newRowLimit != R) {
            MatrixData<ValueT>::setRowLimit(R);
        }
        if (newColLimit != C) {
            MatrixData<ValueT>::setColLimit(R);
        }
    }
    virtual MatrixElement<ValueT> element(index_t N) const override
    {
        return {N % R, N / R, dVec[N]};
    }
    virtual void start() override
    {
        MatrixData<ValueT>::cur = 0;
        Rctr = 0;
        Cctr = 0;
    }

    virtual MatrixElement<ValueT> next() override
    {
        MatrixElement<ValueT> tp{Rctr, Cctr, dVec[MatrixData<ValueT>::cur]};
        ++MatrixData<ValueT>::cur;
        ++Rctr;
        if (Rctr == R) {
            Rctr = 0;
            ++Cctr;
        }
        return tp;
    }

    virtual ValueT at(index_t rowN, index_t colN) const override { return dVec[colN * R + rowN]; }
    auto begin() { return MatrixIteratorCompact(this, 0); }
    auto end() { return MatrixIteratorCompact(this, R * C); }

  protected:
    class MatrixIteratorCompact {
      public:
        explicit MatrixIteratorCompact(const MatrixDataCompact<R, C, ValueT>* MatrixData,
                                       index_t start = 0): mDC(MatrixData), counter(start)
        {
            if (start == mDC->size()) {
                Rctr = R;
                Cctr = C;
            }
        }

        virtual MatrixIteratorCompact& operator++()
        {
            ++counter;
            ++Rctr;
            if (Rctr == R) {
                Rctr = 0;
                ++Cctr;
            }
            return *this;
        }

        virtual MatrixElement<ValueT> operator*() const { return {Rctr, Cctr, mDC->dVec[counter]}; }

      private:
        const MatrixDataCompact<R, C, ValueT>* mDC;
        index_t Rctr{0};
        index_t Cctr{0};
        index_t counter;
    };
};
