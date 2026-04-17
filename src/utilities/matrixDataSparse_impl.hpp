/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "matrixDataSparse.hpp"
#include <algorithm>
#include <vector>

template<typename ValueT>
void matrixDataSparse<ValueT>::assign(index_t row, index_t col, ValueT num)
{
    assert(row != kNullLocation);
    assert(col != kNullLocation);
    assert(row < matrixData<ValueT>::rowLimit());
    assert(col < matrixData<ValueT>::colLimit());
    // assert (std::isfinite (num));
    assert(static_cast<int>(row) >= 0);
    assert(static_cast<int>(col) >= 0);

    // data.push_back (cLoc (X, Y, num));
    data_.emplace_back(row, col, num);
}

template<typename ValueT>
void matrixDataSparse<ValueT>::sortIndex(sparse_ordering ordering)
{
    switch (ordering) {
        case sparse_ordering::column_ordered:
            std::sort(data_.begin(), data_.end(), compareCol<ValueT>);
            break;
        case sparse_ordering::row_ordered:
            std::sort(data_.begin(), data_.end(), compareRow<ValueT>);
            break;
    }
    sortCount_ = static_cast<count_t>(data_.size());
}

template<typename ValueT>
void matrixDataSparse<ValueT>::compact()
{
    if (data_.empty()) {
        return;
    }
    if (!isSorted()) {
        sortIndex(sparse_ordering::column_ordered);
    }

    auto currentDataLocation = data_.begin();
    auto testDataLocation = currentDataLocation + 1;
    auto dataEnd = data_.end();
    while (testDataLocation != dataEnd) {
        // Check if the next is equal to the previous in location. if
        // they are add them; if not shift the new one to the right
        // location and move on.
        if ((testDataLocation->col == currentDataLocation->col) &&
            (testDataLocation->row == currentDataLocation->row)) {
            currentDataLocation->data += testDataLocation->data;
        } else {
            ++currentDataLocation;
            *currentDataLocation = *testDataLocation;
        }
        ++testDataLocation;
    }
    // reduce the size and indicate that we are still sorted.

    data_.resize(++currentDataLocation - data_.begin());
    sortCount_ = static_cast<count_t>(data_.size());
}

template<typename ValueT>
std::vector<count_t> matrixDataSparse<ValueT>::columnCount()
{
    if (!isSorted()) {
        sortIndex(sparse_ordering::column_ordered);
    }
    auto dataEnd = data_.end();
    std::vector<count_t> colCount((*(dataEnd - 1)).row, 0);
    count_t cnt = 1;

    index_t testRow = data_.front().row;
    for (auto testData = data_.begin(); testData != dataEnd; ++testData) {
        if (testRow != testData->row) {
            colCount[testRow] = cnt;
            cnt = 0;
            testRow = testData->row;
        }
        ++cnt;
    }
    colCount[testRow] = cnt;
    return colCount;
}

template<typename ValueT>
ValueT matrixDataSparse<ValueT>::at(index_t rowN, index_t colN) const
{
    if (isSorted()) {
        auto res = std::lower_bound(data_.begin(),
                                    data_.end(),
                                    matrixElement<ValueT>{rowN, colN, ValueT{0}},
                                    compareCol<ValueT>);
        if (res == data_.end()) {
            return ValueT{0};
        }
        if ((res->row == rowN) && (res->col == colN)) {
            return res->data;
        }
    } else {
        for (const auto& rv : data_) {
            if ((rv.row == rowN) && (rv.col == colN)) {
                return rv.data;
            }
        }
    }
    return ValueT{0};
}

template<typename ValueT>
void matrixDataSparse<ValueT>::scale(ValueT factor, index_t startIndex, count_t count)
{
    if (startIndex >= static_cast<index_t>(data_.size())) {
        return;
    }
    auto res = data_.begin() + startIndex;
    auto term = data_.end();
    if (count < static_cast<count_t>(data_.size())) {
        term = data_.begin() + std::min(startIndex + count, static_cast<count_t>(data_.size()));
    }
    while (res != term) {
        res->data *= factor;
        ++res;
    }
}

template<typename ValueT>
void matrixDataSparse<ValueT>::copyReplicate(const matrixDataSparse<ValueT>& a2,
                                             index_t origCol,
                                             std::vector<index_t> newIndices,
                                             std::vector<ValueT> mult)
{
    for (const auto& res : a2.data_) {
        if (res.col == origCol) {
            for (index_t nn = 0; nn < static_cast<index_t>(newIndices.size()); ++nn) {
                data_.emplace_back(res.row, newIndices[nn], res.data * mult[nn]);
            }
        } else {
            data_.push_back(res);
        }
    }
}

template<typename ValueT>
void matrixDataSparse<ValueT>::filter(index_t rowTest)
{
    auto rem =
        std::remove_if(data_.begin(),
                       data_.end(),
                       [rowTest,
                        clim = matrixData<ValueT>::colLimit(),
                        rlim = matrixData<ValueT>::rowLimit()](const matrixElement<ValueT>& el) {
                           return ((el.row == rowTest) || (el.row >= rlim) || (el.col >= clim));
                       });

    data_.erase(rem, data_.end());
}

template<typename ValueT>
void matrixDataSparse<ValueT>::cascade(matrixDataSparse<ValueT>& a2, index_t elementIndex)
{
    auto term = data_.size();
    size_t nn = 0;
    ValueT keyval = ValueT{0};
    while (nn != term) {
        if (data_[nn].col == elementIndex) {
            size_t mm = 0;
            keyval = data_[nn].data;
            for (size_t kk = 0; kk < a2.data_.size(); kk++) {
                if (a2.data_[kk].row == elementIndex) {
                    if (mm == 0) {
                        data_[nn].col = a2.data_[kk].col;
                        data_[nn].data = a2.data_[kk].data * keyval;
                        ++mm;
                    } else {
                        // data.push_back (
                        //     cLoc (std::get<adRow> (data[nn]),
                        //           std::get<adCol> (a2->data[kk]),
                        //           keyval * std::get<adVal> (a2->data[kk])));
                        data_.emplace_back(data_[nn].row,
                                           a2.data_[kk].col,
                                           keyval * a2.data_[kk].data);
                        ++mm;
                    }
                }
            }
        }
        ++nn;
    }
}

template<typename ValueT>
std::vector<ValueT> matrixDataSparse<ValueT>::vectorMult(std::vector<ValueT> V)
{
    sortIndex(sparse_ordering::row_ordered);
    auto maxRow = data_.back().row;
    std::vector<ValueT> out(maxRow, 0);
    auto res = data_.begin();
    auto term = data_.end();
    for (index_t nn = 0; nn <= maxRow; ++nn) {
        while ((res != term) && (res->row == nn)) {
            out[nn] += res->data * V[res->col];
            ++res;
        }
    }
    return out;
}

template<class ValueT>
std::vector<index_t> findMissing(matrixDataSparse<ValueT>& md)
{
    std::vector<index_t> missing;
    md.compact();
    md.sortIndex(sparse_ordering::row_ordered);
    index_t pp = 0;
    for (index_t kk = 0; kk < md.rowLimit(); ++kk) {
        bool good = false;
        if (pp > md.size()) {
            break;
        }
        auto element = md.element(pp);
        while (element.row <= kk) {
            if ((element.row == kk) && (std::isnormal(element.data))) {
                good = true;
                ++pp;
                break;
            }

            ++pp;
            if (pp >= md.size()) {
                break;
            }
            element = md.element(pp);
        }
        if (!good) {
            missing.push_back(kk);
        }
    }
    return missing;
}

template<class ValueT>
std::vector<std::vector<index_t>> findRank(matrixDataSparse<ValueT>& md)
{
    std::vector<index_t> vr, vt;
    std::vector<ValueT> vq, vtq;
    std::vector<std::vector<index_t>> mrows;
    md.sortIndex(sparse_ordering::column_ordered);
    md.compact();
    md.sortIndex(sparse_ordering::row_ordered);
    ValueT factor{0};
    index_t pp{0};
    index_t qq{0};
    auto mp = md.size();
    bool good = false;
    for (index_t kk = 0; kk < md.rowLimit() - 1; ++kk) {
        vr.clear();
        vq.clear();
        auto element = md.element(pp);
        while (element.row == kk) {
            vr.push_back(element.col);
            vq.push_back(element.data);
            ++pp;
            element = md.element(pp);
        }
        qq = pp;
        for (index_t nn = kk + 1; nn < md.rowLimit(); ++nn) {
            vt.clear();
            vtq.clear();
            good = false;
            if (element.col != vr[0]) {
                good = true;
            }
            while (element.row == nn) {
                if (!good) {
                    vt.push_back(element.col);
                    vtq.push_back(element.data);
                }
                ++qq;

                if (qq >= mp) {
                    break;
                }
                element = md.element(qq);
            }
            if (!good) {
                continue;
            }
            if (vr.size() != vtq.size()) {
                continue;
            }

            for (size_t jj = 0; jj < vr.size(); ++jj) {
                if (vt[jj] != vr[jj]) {
                    good = true;
                    break;
                }
                if (jj == 0) {
                    factor = vtq[jj] / vq[jj];
                } else if (std::abs(vtq[jj] / vq[jj] - factor) > 0.000001) {
                    good = true;
                    break;
                }
            }
            if (good) {
                continue;
            }
            mrows.emplace_back(kk, nn);
        }
    }
    return mrows;
}
