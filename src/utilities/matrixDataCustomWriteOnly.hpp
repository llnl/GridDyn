#pragma once

/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

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
