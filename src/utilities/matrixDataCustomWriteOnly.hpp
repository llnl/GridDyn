/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "utilities/matrixData.hpp"
#include <cassert>
#include <functional>
#include <utility>

/** @brief matrix data wrapper around an insert function for matrix elements
none of the other read or assign functions are operation and all will assert false
*/
template<class ValueT = double>
class matrixDataCustomWriteOnly: public matrixData<ValueT> {
  private:
    std::function<void(index_t, index_t, ValueT)> insertFunction;

  public:
    matrixDataCustomWriteOnly() = default;

    void clear() override {};
    void assign(index_t row, index_t col, ValueT num) override { insertFunction(row, col, num); }

    count_t size() const override { return 0; };
    count_t capacity() const override { return 0; };
    matrixElement<ValueT> element(index_t /*N*/) const override
    {
        assert(false);
        return matrixElement<ValueT>();
    }

    ValueT at(index_t /*rowN*/, index_t /*colN*/) const override { return 0.0; };

    void setFunction(std::function<void(index_t, index_t, ValueT)> func)
    {
        insertFunction = std::move(func);
    }
};
