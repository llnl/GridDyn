/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "matrixDataContainer.hpp"
#include <algorithm>
#include <vector>

/** @brief class implementation filter for another matrixData object
most functions are just simple forwarding to the underlying matrixData object
except the assign operator, the row index is checked against a set of filtered rows
if it is not in the filtered list it is passed through to the underlying matrix
*/
template<class ValueT = double>
class matrixDataFilter: public matrixDataContainer<ValueT> {
  private:
    std::vector<index_t> rowFilter;  //!< the vector of translations
  public:
    /** @brief constructor
     */
    matrixDataFilter() = default;

    explicit matrixDataFilter(matrixData<ValueT>& input): matrixDataContainer<ValueT>(input) {}

    void assign(index_t row, index_t col, ValueT num) override
    {
        bool found = std::binary_search(rowFilter.begin(), rowFilter.end(), row);
        if (!found) {
            matrixDataContainer<ValueT>::md->assign(row, col, num);
        }
    }

    /** add a filter
    @param[in] row  the row to filter out
    */
    void addFilter(index_t row)
    {
        auto lb = std::lower_bound(rowFilter.begin(), rowFilter.end(), row);
        if (lb == rowFilter.end()) {
            rowFilter.push_back(row);
        } else if (*lb != row) {
            rowFilter.insert(lb, row);
        }
    }

    /** add a vector of filters
    @param[in] rows  the rows to filter out
    */
    void addFilter(std::vector<index_t>& rows)
    {
        rowFilter.reserve(rowFilter.size() + rows.size());
        rowFilter.insert(rowFilter.end(), rows.begin(), rows.end());
        std::sort(rowFilter.begin(), rowFilter.end());
    }
};
