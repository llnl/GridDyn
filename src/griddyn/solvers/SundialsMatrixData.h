/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "sunmatrix/sunmatrix_dense.h"
#include "sunmatrix/sunmatrix_sparse.h"
#include "utilities/MatrixData.hpp"
#include "utilities/MatrixDataOrdering.hpp"
#include <cstdio>
#include <memory>

namespace griddyn::solvers {
/** @brief class implementing an MatrixData wrapper around the SUNDIALS dense matrix*/
class SundialsMatrixDataDense: public MatrixData<double> {
  private:
    SUNMatrix J = nullptr;  //!< the vector of tuples containing the data
  public:
    /** @brief compact constructor
     */
    SundialsMatrixDataDense() = default;
    /** @brief alternate constructor defining the Dense matrix to fill
@param[in] mat the dense SUNDIALS matrix*/
    explicit SundialsMatrixDataDense(SUNMatrix mat);

    void clear() override;

    void assign(index_t x, index_t y, double num) override;

    /** set the SUNDIALS matrix
@param[in] mat the dense SUNDIALS matrix
*/
    void setMatrix(SUNMatrix mat);

    count_t size() const override;

    count_t capacity() const override;

    MatrixElement<double> element(index_t n) const override;

    double at(index_t rowN, index_t colN) const override;
};

class SundialsMatrixDataSparseColumn: public MatrixData<double> {
  private:
    SUNMatrix J = nullptr;  //!< pointer to the sundials sparse matrix
    index_t ccol = 0;

  public:
    /** @brief compact constructor
     */
    SundialsMatrixDataSparseColumn() = default;
    /** @brief alternate constructor defining the Sparse matrix to fill*/
    explicit SundialsMatrixDataSparseColumn(SUNMatrix mat);

    void clear() override;

    void assign(index_t row, index_t col, double num) override;

    /** set the SUNDIALS matrix
@param[in] mat the sparse SUNDIALS matrix
*/
    void setMatrix(SUNMatrix mat);

    count_t size() const override;

    count_t capacity() const override;

    MatrixElement<double> element(index_t n) const override;

    double at(index_t rowN, index_t colN) const override;

    virtual void start() override;

    virtual MatrixElement<double> next() override;
};

/** @brief class implementing an MatrixData wrapper around the SUNDIALS dense matrix*/
class SundialsMatrixDataSparseRow: public MatrixData<double> {
  private:
    SUNMatrix J;  //!< the vector of tuples containing the data
    index_t crow = 0;  //!< the current row of access

  public:
    /** @brief compact constructor
     */
    SundialsMatrixDataSparseRow() = default;
    /** @brief alternate constructor defining the Sparse matrix to fill*/
    explicit SundialsMatrixDataSparseRow(SUNMatrix mat);

    void clear() override;

    void assign(index_t row, index_t col, double num) override;

    /** set the SUNDIALS matrix
@param[in] mat the sparse SUNDIALS matrix
*/
    void setMatrix(SUNMatrix mat);

    count_t size() const override;

    count_t capacity() const override;

    MatrixElement<double> element(index_t n) const override;

    double at(index_t rowN, index_t colN) const override;

    virtual void start() override;

    virtual MatrixElement<double> next() override;
};

std::unique_ptr<MatrixData<double>> makeSundialsMatrixData(SUNMatrix j);

}  // namespace griddyn::solvers
