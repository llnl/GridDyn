/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "matrixDataOrdering.hpp"
#include "utilities/matrixData.hpp"
#include <utility>
#include <vector>

/** @brief class implementing an expandable sparse matrix with an expandable data vector for each
 *row also adding a function to get all the data in a particular row.
 */
template<sparse_ordering M = sparse_ordering::row_ordered, class ValueT = double>
class matrixDataOrdered: public matrixData<ValueT> {
  private:
    std::vector<std::vector<std::pair<index_t, ValueT>>>
        dVec;  //!< vector of data vectors for each Row

    decltype(dVec[0].cbegin()) cptr;  //!< ptr to the beginning of the sequence;
    decltype(dVec[0].cend()) iend;  //!< ptr to the end of the current sequence;
    index_t ci = 0;  //!< indicator of which vector of the array we are sequencing on;
    count_t count = 0;
    count_t primary_max = 0;  //!< the limit on the primary key
  public:
    /** @brief default constructor*/
    matrixDataOrdered() noexcept {}
    /** @brief constructor
    @param[in] rowCount  the number of rows and columns in the matrix
    */
    explicit matrixDataOrdered(index_t RowCount):
        matrixData<ValueT>(RowCount, RowCount), dVec(RowCount)
    {
    }
    void clear() override
    {
        count = 0;
        for (auto& dvk : dVec) {
            dvk.clear();
        }
    }

    void assign(index_t row, index_t col, ValueT num) override
    {
        auto key = keyOrder<index_t, M>::order(row, col);
        auto iI = dVec[key.first].begin();
        auto iEnd = dVec[key.first].end();
        while (iI != iEnd) {
            if (iI->first == key.second) {
                iI->second += num;
                break;
            }
            if (iI->first > key.second) {
                dVec[key.first].insert(iI, std::make_pair(key.second, num));
                ++count;
                break;
            }
            ++iI;
        }
        if (iI == iEnd) {
            dVec[key.first].emplace_back(key.second, num);
            ++count;
        }
    }

    virtual void limitUpdate(index_t newRowLimit, index_t newColLimit) override
    {
        primary_max = keyOrder<index_t, M>::primary(newRowLimit, newColLimit);
        dVec.resize(primary_max);
    }

    virtual void reserve(count_t reserveSize) override
    {
        // dVec can't have size 0 since that case is taken by a different template specialization
        auto rcount = 3 * (reserveSize / static_cast<count_t>(dVec.size()));
        for (auto& dvk : dVec) {
            dvk.reserve(rcount);
        }
    }
    virtual count_t size() const override { return count; }
    virtual count_t capacity() const override
    {
        count_t sz = 0;
        for (auto& dvk : dVec) {
            sz += static_cast<count_t>(dvk.capacity());
        }
        return sz;
    }
    matrixElement<ValueT> element(index_t N) const override
    {
        index_t ii = 0;

        auto sz2 = dVec[0].size();
        decltype(sz2) sz1 = 0;
        while (N >= static_cast<count_t>(sz2)) {
            sz1 += dVec[ii].size();
            ++ii;
            sz2 += dVec[ii].size();
        }
        auto res = keyOrder<index_t, M>::order(ii, dVec[ii][N - sz1].first);
        return {res.first, res.second, dVec[ii][N - sz1].second};
    }

    virtual void start() override
    {
        ci = 0;
        while (dVec[ci].empty()) {
            ++ci;
            if (ci == primary_max) {
                cptr = dVec[ci - 1].cbegin();
                iend = dVec[ci - 1].cend();
                break;
            }
        }
        if (ci != primary_max) {
            cptr = dVec[ci].cbegin();
            iend = dVec[ci].cend();
        }
    }

    matrixElement<ValueT> next() override
    {
        auto res = keyOrder<index_t, M>::order(ci, cptr->first);
        matrixElement<ValueT> tp{res.first, res.second, cptr->second};
        ++cptr;
        if (cptr == iend) {
            ++ci;
            if (ci < primary_max) {
                while (dVec[ci].empty()) {
                    ++ci;
                    if (ci == primary_max) {
                        cptr = dVec[ci].cbegin();
                        iend = dVec[ci].cend();
                        break;
                    }
                }
                if (ci < primary_max) {
                    cptr = dVec[ci].cbegin();
                    iend = dVec[ci].cend();
                }
            }
        }
        return tp;
    }

    bool moreData() override { return (ci < primary_max); }
    ValueT at(index_t rowN, index_t colN) const override
    {
        auto key = keyOrder<index_t, M>::order(rowN, colN);
        for (auto& de : dVec[key.first]) {
            if (de.first == key.second) {
                return de.second;
            }
            if (de.first > key.second) {
                return ValueT(0);
            }
        }
        return ValueT(0);
    }

    const std::vector<std::pair<index_t, ValueT>>& getSet(index_t N) const { return dVec[N]; }
};
