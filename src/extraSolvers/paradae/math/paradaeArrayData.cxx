/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DISABLE_GRIDDYN

#    include "paradaeArrayData.h"

namespace griddyn::paradae {
    paradaeArrayData::paradaeArrayData(SparseMatrix* mat)
    {
        J = mat;
        setRowLimit(static_cast<count_t>(mat->GetM()));

        setColLimit(static_cast<count_t>(mat->GetM()));
    }

    void paradaeArrayData::clear()
    {
        J->FillKeepingStruct(0);
    }

    void paradaeArrayData::assign(index_t X, index_t Y, double num)
    {
        (*J)(X, Y) += num;
    }

    void paradaeArrayData::setMatrix(SparseMatrix* mat)
    {
        J = mat;
        setRowLimit(static_cast<count_t>(mat->GetM()));
        setColLimit(static_cast<count_t>(mat->GetM()));
    }

    count_t paradaeArrayData::size() const
    {
        return static_cast<count_t>(J->GetNNZ());
    }

    count_t paradaeArrayData::capacity() const
    {
        return static_cast<count_t>(J->GetNNZ());
    }

    index_t paradaeArrayData::rowIndex(index_t N) const
    {
        return J->GetRowIndex(N);
    }

    index_t paradaeArrayData::colIndex(index_t N) const
    {
        return J->GetColIndex(N);
    }

    double paradaeArrayData::val(index_t N) const
    {
        return J->GetValue(N);
    }

    double paradaeArrayData::at(index_t rowN, index_t colN) const
    {
        return (*J)(rowN, colN);
    }

    matrixElement<double> paradaeArrayData::element(index_t N) const
    {
        return {J->GetRowIndex(N), J->GetColIndex(N), J->GetValue(N)};
    }

}  // namespace griddyn::paradae

#endif
