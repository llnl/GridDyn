/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../math/SparseMatrix.h"
#include "utilities/matrixData.hpp"

namespace griddyn::paradae {
    class paradaeArrayData: public matrixData<double> {
      private:
        SparseMatrix* J = nullptr;

      public:
        paradaeArrayData() {};
        paradaeArrayData(SparseMatrix* mat);

        void clear();
        void assign(index_t X, index_t Y, double num);
        void setMatrix(SparseMatrix* mat);
        count_t size() const;
        count_t capacity() const;
        index_t rowIndex(index_t N) const;
        index_t colIndex(index_t N) const;
        double val(index_t N) const;
        double at(index_t rowN, index_t colN) const;
        matrixElement<double> element(index_t N) const;
    };
}  // namespace griddyn::paradae
