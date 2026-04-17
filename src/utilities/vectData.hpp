/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "indexTypes.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

/**
 * class for storing data from the Jacobian computation
 */
template<class Y = double>
class vectData {
  public:
    using vLoc = std::pair<index_t, Y>;

  private:
    static bool compareLocVectData(const vLoc& A, const vLoc& B) { return (A.first < B.first); }
    std::vector<vLoc> dVec;  //!< the vector of pairs containing the data
    count_t maxRowCount = kNullLocation;  //!< the maximum row number
  public:
    vectData() {}
    /**
     * function to clear the data
     */
    void reset() { dVec.resize(0); }
    /**
     * add a new Jacobian element
     * @param[in] X the row (X) and column(Y) of the element
     * @param[in] num the value of the element
     */
    void assign(index_t X, Y num)
    {
        assert(!std::isnan(num));
        assert(X < maxRowCount);
        dVec.emplace_back(X, num);
    }
    /** assign with a check
     * add a new Jacobian element if the arguments are valid (X>=0) &&(Y>=0)
     * @param[in] X the row (X) of the element
     * @param[in] num the value of the element
     */
    void assignCheck(index_t X, Y num)
    {
        if ((X >= 0) && (X < maxRowCount)) {
            assert(!std::isnan(num));
            dVec.emplace_back(X, num);
        }
    }
    /**
     * reserve space for the count of the Jacobian
     * @param[in] size the amount of space to reserve
     */
    void reserve(count_t size) { dVec.reserve(size); }
    /**
     * get the number of points
     * @return the number of points
     */
    count_t points() const { return static_cast<count_t>(dVec.size()); }
    /**
     * get the maximum number of points the vector can hold
     * @return the number of points
     */
    count_t capacity() const { return static_cast<count_t>(dVec.capacity()); }
    /**
     * sort the index based first on row number than column number
     */
    void sortIndex() { std::sort(dVec.begin(), dVec.end(), compareLocVectData); }
    /**
     * compact the index merging values with the same row and column number together
     */
    void compact()
    {
        if (dVec.empty()) {
            return;
        }

        auto dvb = dVec.begin();
        auto dv2 = dvb;
        ++dv2;
        auto dvend = dVec.end();
        while (dv2 != dvend) {
            if (dv2->first == dvb->first) {
                dvb->second += dv2->second;
            } else {
                ++dvb;
                *dvb = *dv2;
            }
            ++dv2;
        }
        dVec.resize(++dvb - dVec.begin());
    }
    /**
     * get the row value
     * @param[in] N the element number to return
     * @return the row of the corresponding index
     */
    index_t rowIndex(index_t N) { return dVec[N].first; }
    /**
     * get the column value
     * @param[in] N the element number to return
     * @return the column of the corresponding index
     */

    Y val(index_t N) { return dVec[N].second; }
    /**
     * get the number nonzero of elements in each row
     * @return a vector of integers with the column counts
     */

    Y at(index_t rowN)
    {  // NOTE: function assumes vectData is sorted and compacted
        auto res = std::lower_bound(dVec.begin(), dVec.end(), vLoc(rowN, Y{0}), compareLocVectData);
        if (res == dVec.end()) {
            return Y{0};
        }
        if (res->first == rowN) {
            return res->second;
        }
        return Y{0};
    }

    void scale(Y factor, index_t start = 0, count_t count = 0x7FFFFFFF)
    {
        if (start >= dVec.size()) {
            return;
        }
        auto res = dVec.begin() + start;
        auto term = dVec.begin() + std::min(start + count, static_cast<count_t>(dVec.size()));

        while (res != term) {
            res->second *= factor;
            ++res;
        }
    }
};
