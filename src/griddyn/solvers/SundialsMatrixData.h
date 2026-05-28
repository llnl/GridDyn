/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "sunmatrix/sunmatrix_dense.h"
#include "sunmatrix/sunmatrix_sparse.h"
#include "utilities/matrixData.hpp"
#include "utilities/matrixDataOrdering.hpp"
#include <cstdio>
#include <memory>

namespace griddyn::solvers {
/** @brief class implementing an matrixData wrapper around the SUNDIALS dense matrix*/
class SundialsMatrixDataDense: public matrixData<double> {
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

    void assign(index_t X, index_t Y, double num) override;

    /** set the SUNDIALS matrix
@param[in] mat the dense SUNDIALS matrix
*/
    void setMatrix(SUNMatrix mat);

    count_t size() const override;

    count_t capacity() const override;

    matrixElement<double> element(index_t N) const override;

    double at(index_t rowN, index_t colN) const override;
};

class SundialsMatrixDataSparseColumn: public matrixData<double> {
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

    matrixElement<double> element(index_t N) const override;

    double at(index_t rowN, index_t colN) const override;

    virtual void start() override;

    virtual matrixElement<double> next() override;
};

/** @brief class implementing an matrixData wrapper around the SUNDIALS dense matrix*/
class SundialsMatrixDataSparseRow: public matrixData<double> {
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

    matrixElement<double> element(index_t N) const override;

    double at(index_t rowN, index_t colN) const override;

    virtual void start() override;

    virtual matrixElement<double> next() override;
};

std::unique_ptr<matrixData<double>> makeSundialsMatrixData(SUNMatrix J);

}  // namespace griddyn::solvers
