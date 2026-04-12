/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#pragma once

#include "matrixData.hpp"
#include "matrixDataOrdering.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

/** @brief simple class for compute the keyIndex from a row and column and inverting it*/
template<class X, sparse_ordering M>
class keyCompute {  // default is row ordered
  public:
    static X keyGen(index_t rowIndex, index_t colIndex)
    {
        return ((static_cast<X>(rowIndex) << vshift) + static_cast<X>(colIndex));
    }

    static index_t row(X key) { return static_cast<index_t>(key >> vshift); }
    static index_t col(X key) { return static_cast<index_t>(key & mask); }

  private:
    static constexpr unsigned int vshift = 4 * sizeof(X);
    static constexpr X mask = (X(1) << vshift) - 1;
};

template<class X>
class keyCompute<X, sparse_ordering::column_ordered> {
  public:
    static X keyGen(index_t rowIndex, index_t colIndex)
    {
        return ((static_cast<X>(colIndex) << vshift) + static_cast<X>(rowIndex));
    }

    static index_t col(X key) { return static_cast<index_t>(key >> vshift); }
    static index_t row(X key) { return static_cast<index_t>(key & mask); }

  private:
    static constexpr unsigned int vshift = 4 * sizeof(X);
    static constexpr X mask = (X(1) << vshift) - 1;
};

/** @brief simple class for compute the blockIndex from a row and column and inverting it*/
template<count_t K, sparse_ordering M>
class blockCompute {
  public:
    index_t blockIndexGen(index_t row, index_t col) const
    {
        return (((M == sparse_ordering::row_ordered) ? row : col) + bias) >> kShift;
    };

    void setMaxIndex(index_t rowMax, index_t colMax)
    {
        index_t keyMax = (M == sparse_ordering::row_ordered) ? rowMax : colMax;
        index_t remaining = keyMax;
        int shiftCount = 0;
        while (remaining > 1) {
            remaining >>= 1;
            shiftCount++;
        }
        // set kShift to get 2^K bins from shifting the ValueT column
        kShift = shiftCount - K + 1;
        // figure out how many elements should be in the first and last buckets
        index_t extraElements = keyMax - (1 << kShift) * ((1 << K) - 2);
        bias = (1 << kShift) - (extraElements >> 1);
        if (extraElements < (1_ind << (kShift - K))) {
            bias >>= 1;  // this just evens it out a little more
        }
    }

  private:
    int kShift = 0;  //!< the shift factor used for column determination
    int bias = 0;  //!< the bias added to the column to even out the number of points in the first
                   //!< and last columns
};

/** @brief simple class for compute the blockIndex from a row and column and inverting it*/
template<sparse_ordering M>
class blockCompute<1, M> {
  public:
    index_t blockIndexGen(index_t row, index_t col) const
    {
        return (((M == sparse_ordering::row_ordered) ? row : col) >= split);
    }

    void setMaxIndex(index_t rowMax, index_t colMax)
    {
        index_t key = (M == sparse_ordering::row_ordered) ? rowMax : colMax;
        split = (key >> 1);
    }

  private:
    index_t split = 0;  //!< the shift factor used for column determination
};

/** @brief class implementing an expandable sparse matrix with multiple column vectors
 sparseSMB implements a sparse matrix using a vector and not doing the sort until absolutely
necessary the vectors contains a single 64 bit unsigned number composed by using col<<32+row
allowing support for up to 2^32-2 rows and columns to make the sorting faster when done the template
parameter K indicates that the structure should use 2^K vectors for data storage the space allocated
is approximately 2x that used in the single column structure
*/
template<count_t K,
         class X = std::uint32_t,
         class ValueT = double,
         sparse_ordering M = sparse_ordering::row_ordered>
class matrixDataSparseSMB: public matrixData<ValueT> {
    static_assert(std::is_integral<X>::value, "class X must be of integral type");
    static_assert(std::is_unsigned<X>::value, "class X must be of an unsigned type");
    using pLoc = std::pair<X, ValueT>;
    using cIterator = typename std::vector<pLoc>::const_iterator;

  private:
    keyCompute<X, M>
        key_computer;  //!< object that generators the keys and extracts row and column information
    std::array<std::vector<pLoc>, (1 << K)> dVec;  //!< the vector of pairs containing the data
    count_t sortCount = 0;  //!< count of the last sort operation
    blockCompute<K, M> block_computer;  //!< object that generates the appropriate block index;
    cIterator cptr;  //!< ptr to the beginning of the sequence;
    cIterator iend;  //!< ptr to the end of the current sequence;
    int ci = 0;  //!< indicator of which vector of the array we are sequencing on;
  public:
    /** @brief constructor
      the actual storage space used is approx 2x startCount due to expected unevenness in the array
    @param[in] startCount  the number of elements to reserve
    */
    explicit matrixDataSparseSMB(index_t startCount = 1000)
    {
        for (auto& dvk : dVec) {
            dvk.reserve(startCount >> (K - 1));
        }
    };
    void clear() override
    {
        for (auto& dvk : dVec) {
            dvk.clear();
        }
    };

    void assign(index_t row, index_t col, ValueT num) override
    {
        auto temp = block_computer.blockIndexGen(row, col);
        assert(temp < (1 << K));
        dVec[temp].emplace_back(key_computer.keyGen(row, col), num);
    }

    void reserve(count_t reserveSize) override
    {
        for (auto& dvk : dVec) {
            dvk.reserve(reserveSize >> (K - 1));
        }
    }
    count_t size() const override
    {
        count_t sz = 0;
        for (auto& dvk : dVec) {
            sz += static_cast<count_t>(dvk.size());
        }
        return sz;
    }

    virtual void limitUpdate(index_t newRowLimit, index_t newColLimit) override
    {
        block_computer.setMaxIndex(newRowLimit, newColLimit);
    }

    count_t capacity() const override
    {
        count_t sz = 0;
        for (auto& dvk : dVec) {
            sz += static_cast<count_t>(dvk.capacity());
        }
        return sz;
    }
    /**
     * @brief sort the index based first on column number then column number
     */
    void sortIndex()
    {
        // std::sort(dVec.begin(), dVec.end(), compareLocSM);
        auto fp = [](const pLoc& A, const pLoc& B) { return (A.first < B.first); };
        for (auto& dvk : dVec) {
            std::sort(dvk.begin(), dvk.end(), fp);
        }
        sortCount = size();
    }

    /**
     * @brief sort the index based first on row number then row number
     */
    void compact() override
    {
        if (!isSorted()) {
            sortIndex();
        }
        for (auto& dvk : dVec) {
            if (dvk.empty()) {
                return;
            }

            auto dvb = dvk.begin();
            auto dv2 = dvb;
            ++dv2;
            auto dvend = dvk.end();
            while (dv2 != dvend) {
                if (dv2->first == dvb->first) {
                    dvb->second += dv2->second;
                } else {
                    ++dvb;
                    *dvb = *dv2;
                }
                ++dv2;
            }
            dvk.resize(++dvb - dvk.begin());
        }
        sortCount = size();
    }

    matrixElement<ValueT> element(index_t N) const override
    {
        int ii = 0;
        index_t sz1 = 0;
        auto sz2 = static_cast<index_t>(dVec[0].size());
        while (N >= sz2) {
            sz1 += static_cast<index_t>(dVec[ii].size());
            ++ii;
            sz2 += static_cast<index_t>(dVec[ii].size());
        }
        assert(ii < (1 << K));
        return {key_computer.row(dVec[ii][N - sz1].first),
                key_computer.col(dVec[ii][N - sz1].first),
                dVec[ii][N - sz1].second};
    }

    auto begin() const { return matrixIteratorSM(this, 0); }
    auto end() const { return matrixIteratorSM(this, size()); }
    void start() override
    {
        constexpr int bucketCount = (1 << K);
        ci = 0;
        while ((ci < bucketCount) && dVec[ci].empty()) {
            ++ci;
        }
        if (ci < bucketCount) {
            cptr = dVec[ci].cbegin();
            iend = dVec[ci].cend();
        }
    }

    matrixElement<ValueT> next() override
    {
        constexpr int bucketCount = (1 << K);
        matrixElement<ValueT> tp{key_computer.row(cptr->first),
                                 key_computer.col(cptr->first),
                                 cptr->second};
        ++cptr;
        if (cptr == iend) {
            ++ci;
            while ((ci < bucketCount) && dVec[ci].empty()) {
                ++ci;
            }
            if (ci < bucketCount) {
                cptr = dVec[ci].cbegin();
                iend = dVec[ci].cend();
            }
        }
        return tp;
    }

    bool moreData() override { return (ci < (1 << K)) && (cptr != iend); }
    /** @brief check if the sparse array is sorted
    @return bool indicating sorted status
    */
    bool isSorted() const { return (sortCount == size()); }
    ValueT at(index_t rowN, index_t colN) const override
    {
        auto cmp = key_computer.keyGen(rowN, colN);
        auto I = block_computer.blockIndexGen(rowN, colN);
        if (isSorted()) {
            auto res = std::lower_bound(dVec[I].begin(),
                                        dVec[I].end(),
                                        std::make_pair(cmp, 0),
                                        [](pLoc A, pLoc B) { return (A.first < B.first); });
            if (res == dVec[I].end()) {
                return ValueT(0);
            }
            if (res->first == cmp) {
                return res->second;
            }
        } else {
            for (const auto& rv : dVec[I]) {
                if (rv.first == cmp) {
                    return rv.second;
                }
            }
        }
        return ValueT(0);
    }

  protected:
    class matrixIteratorSM {
      public:
        explicit matrixIteratorSM(const matrixDataSparseSMB<K, X, ValueT, M>* matrixData,
                                  index_t start = 0): mDS(matrixData)
        {
            if (start == 0) {
                ci = 0;
                while (mDS->dVec[ci].empty()) {
                    ++ci;
                    if (ci == (1 << K)) {
                        --ci;
                        break;
                    }
                }
                cptr = mDS->dVec[ci].cbegin();
                iend = mDS->dVec[ci].cend();
            } else if (start == mDS->size()) {
                ci = static_cast<count_t>(mDS->dVec.size() - 1);
                while (mDS->dVec[ci].size() == 0) {
                    --ci;
                    if (ci < 0) {
                        // if everything is empty just go back to the last one
                        ci = static_cast<count_t>(mDS->dVec.size() - 1);
                    }
                    cptr = mDS->dVec[ci].cend();
                    iend = mDS->dVec[ci].cend();
                }
            }
        }

        matrixIteratorSM& operator++()
        {
            ++cptr;
            if (cptr == iend) {
                ++ci;
                if (ci < (1 << K)) {
                    while (mDS->dVec[ci].empty()) {
                        ++ci;
                        if (ci >= (1 << K)) {
                            --ci;
                            break;
                        }
                    }
                    cptr = mDS->dVec[ci].cbegin();
                    iend = mDS->dVec[ci].cend();
                }
            }
            return *this;
        }

        matrixElement<ValueT> operator*() const
        {
            return {mDS->key_computer.row(cptr->first),
                    mDS->key_computer.col(cptr->first),
                    cptr->second};
        }

      private:
        const matrixDataSparseSMB<K, X, ValueT, M>* mDS = nullptr;
        cIterator cptr;  //!< ptr to the beginning of the sequence;
        cIterator iend;  //!< ptr to the end of the current sequence;
        int ci = 0;  //!< indicator of which vector of the array we are sequencing on;
    };
};

/** @brief class implementing an expandable sparse matrix geared for Jacobian entries
 sparseSM implements a sparse matrix using a vector and not doing the sort until absolutely
necessary the vector contains a single 32 bit unsigned number composed by using col<<16+row to make
the sorting faster when done
*/
template<class X, class ValueT, sparse_ordering M>
class matrixDataSparseSMB<0, X, ValueT, M>: public matrixData<ValueT> {
    static_assert(std::is_integral<X>::value, "class X must be of integral type");
    static_assert(std::is_unsigned<X>::value, "class X must be of an unsigned type");

  private:
    using pLoc = std::pair<X, ValueT>;
    using cIterator = typename std::vector<pLoc>::const_iterator;
    count_t sortCount = 0;  //!< count of the last sort operation
    std::vector<pLoc> dVec;  //!< the vector of pairs containing the data
    cIterator cptr;  //!< ptr to the beginning of the sequence;
    keyCompute<X, M>
        key_computer;  //!< object that generators the keys and extracts row and column information

  public:
    /** @brief constructor
      the actual storage space used is approx 2x startCount due to expected unevenness in the array
    @param[in] startCount  the number of elements to reserve
    */
    explicit matrixDataSparseSMB(index_t startCount = 1000) { dVec.reserve(startCount); }
    void clear() override { dVec.clear(); }
    void assign(index_t row, index_t col, ValueT num) override
    {
        dVec.emplace_back(key_computer.keyGen(row, col), num);
    }

    /**
     * @brief reserve space for the count of the Jacobian elements
     * @param[in] reserveSize the amount of space to reserve
     */
    void reserve(count_t reserveSize) override { dVec.reserve(reserveSize); }
    count_t size() const override { return static_cast<count_t>(dVec.size()); }
    count_t capacity() const override { return static_cast<count_t>(dVec.capacity()); }
    /**
     * @brief sort the index based first on column number then column number
     */
    void sortIndex()
    {
        std::sort(dVec.begin(), dVec.end(), [](pLoc A, pLoc B) { return (A.first < B.first); });
    }
    /**
     * @brief sort the index based first on row number then row number
     */

    void compact() override
    {
        if (dVec.empty()) {
            return;
        }
        if (!isSorted()) {
            sortIndex();
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
        sortCount = size();
    }

    matrixElement<ValueT> element(index_t N) const override
    {
        return {key_computer.row(dVec[N].first), key_computer.col(dVec[N].first), dVec[N].second};
    }

    auto begin() const { return matrixIteratorSM(this, 0); }
    auto end() const { return matrixIteratorSM(this, size()); }
    void start() override { cptr = dVec.cbegin(); }
    matrixElement<ValueT> next() override
    {
        matrixElement<ValueT> tp{key_computer.row(cptr->first),
                                 key_computer.col(cptr->first),
                                 cptr->second};
        ++cptr;
        return tp;
    }

    bool moreData() override { return (cptr != dVec.cend()); }
    /** @brief check if the sparse array is sorted
    @return bool indicating sorted status
    */
    bool isSorted() const { return (sortCount == size()); }
    ValueT at(index_t rowN, index_t colN) const override
    {
        auto cmp = key_computer.keyGen(rowN, colN);
        if (isSorted()) {
            auto res = std::lower_bound(dVec.cbegin(),
                                        dVec.cend(),
                                        std::make_pair(cmp, 0),
                                        [](pLoc A, pLoc B) { return (A.first < B.first); });
            if (res == dVec.cend()) {
                return ValueT(0);
            }
            if (res->first == cmp) {
                return res->second;
            }
        } else {
            for (const auto& rv : dVec) {
                if (rv.first == cmp) {
                    return rv.second;
                }
            }
        }
        return ValueT(0);
    }
    class matrixIteratorSM {
      public:
        explicit matrixIteratorSM(const matrixDataSparseSMB<0, X, ValueT, M>* matrixData,
                                  index_t start = 0): mDS(matrixData)
        {
            if (start == 0) {
                cptr = mDS->dVec.cbegin();
            } else if (start < mDS->size()) {
                cptr = mDS->dVec.cbegin() + start;
            } else {
                cptr = mDS->dVec.cend();
            }
        }

        matrixIteratorSM& operator++() { ++cptr; }
        virtual matrixElement<ValueT> operator*() const override
        {
            return {mDS->key_computer.row(cptr->first),
                    mDS->key_computer.col(cptr->first),
                    cptr->second};
        }

      private:
        const matrixDataSparseSMB<0, X, ValueT, M>* mDS = nullptr;
        cIterator cptr;  //!< ptr to the beginning of the sequence;
    };
};

/** @brief class implementing an expandable sparse matrix with multiple column vectors
 sparseSMB implements a sparse matrix using a vector and not doing the sort until absolutely
necessary the vectors contains a single 64 bit unsigned number composed by using col<<32+row
allowing support for up to 2^32-2 rows and columns to make the sorting faster when done the template
parameter K indicates that the structure should use 2^K vectors for data storage the space allocated
is approximately 2x that used in the single column structure
*/
template<class X, class ValueT, sparse_ordering M>
class matrixDataSparseSMB<1, X, ValueT, M>: public matrixData<ValueT> {
    static_assert(std::is_integral<X>::value, "class X must be of integral type");
    static_assert(std::is_unsigned<X>::value, "class X must be of an unsigned type");
    using pLoc = std::pair<X, ValueT>;
    using cIterator = typename std::vector<pLoc>::const_iterator;

  private:
    int ci = 0;  //!< indicator of which vector of the array we are sequencing on;
    std::array<std::vector<pLoc>, 2> dVec;  //!< the vector of pairs containing the data
    count_t sortCount = 0;  //!< count of the last sort operation

    cIterator cptr;  //!< iterator to the beginning of the sequence;
    cIterator iend;  //!< iterator to the end of the current sequence;

    keyCompute<X, M>
        key_computer;  //!< object that generators the keys and extracts row and column information
    blockCompute<1, M> block_computer;  //!< object that generators the appropriate block to use

  public:
    /** @brief constructor
      the actual storage space used is approx 2x startCount due to expected unevenness in the array
    @param[in] startCount  the number of elements to reserve
    */
    explicit matrixDataSparseSMB(index_t startCount = 1000)
    {
        dVec[0].reserve(startCount);
        dVec[1].reserve(startCount);
    };
    void clear() override
    {
        dVec[0].clear();
        dVec[1].clear();
    };

    void assign(index_t row, index_t col, ValueT num) override
    {
        dVec[block_computer.blockIndexGen(row, col)].emplace_back(key_computer.keyGen(row, col),
                                                                  num);
        // dVec[col / (colLim >> K)].emplace_back((col << 16) + row, num);
    }

    void reserve(count_t reserveSize) override
    {
        dVec[0].reserve(reserveSize);
        dVec[1].reserve(reserveSize);
    }
    count_t size() const override { return static_cast<count_t>(dVec[0].size() + dVec[1].size()); }

    virtual void limitUpdate(index_t newRowLimit, index_t newColLimit) override
    {
        block_computer.setMaxIndex(newRowLimit, newColLimit);
    }

    count_t capacity() const override
    {
        return static_cast<count_t>(dVec[0].capacity() + dVec[1].capacity());
    }
    /**
     * @brief sort the index based first on column number then column number
     */
    void sortIndex()
    {
        // std::sort(dVec.begin(), dVec.end(), compareLocSM);
        auto fp = [](const pLoc& A, const pLoc& B) { return (A.first < B.first); };

        std::sort(dVec[0].begin(), dVec[0].end(), fp);
        std::sort(dVec[1].begin(), dVec[1].end(), fp);
        sortCount = size();
    }

    void compact() override
    {
        if (!isSorted()) {
            sortIndex();
        }
        for (auto& dvk : dVec) {
            if (dvk.empty()) {
                return;
            }

            auto dvb = dvk.begin();
            auto dv2 = dvb;
            ++dv2;
            auto dvend = dvk.end();
            while (dv2 != dvend) {
                if (dv2->first == dvb->first) {
                    dvb->second += dv2->second;
                } else {
                    ++dvb;
                    *dvb = *dv2;
                }
                ++dv2;
            }
            dvk.resize(++dvb - dvk.begin());
        }
        sortCount = size();
    }

    virtual matrixElement<ValueT> element(index_t N) const override
    {
        matrixElement<ValueT> ret;
        auto dvs = static_cast<index_t>(dVec[0].size());
        ret.row = static_cast<index_t>(
            key_computer.row((N >= dvs) ? dVec[1][N - dvs].first : dVec[0][N].first));
        ret.col = static_cast<index_t>(
            key_computer.col((N >= dvs) ? dVec[1][N - dvs].first : dVec[0][N].first));
        ret.data = ((N >= dvs) ? dVec[1][N - dvs].second : dVec[0][N].second);
        return ret;
    }

    auto begin() const { return matrixIteratorSM(this, 0); }
    auto end() const { return matrixIteratorSM(this, size()); }
    void start() override
    {
        cptr = dVec[0].cbegin();
        iend = dVec[0].cend();
        ci = 0;
    }

    matrixElement<ValueT> next() override
    {
        matrixElement<ValueT> tp{key_computer.row(cptr->first),
                                 key_computer.col(cptr->first),
                                 cptr->second};
        ++cptr;
        if (cptr == iend) {
            ++ci;
            if (ci < 2) {
                cptr = dVec[1].cbegin();
                iend = dVec[1].cend();
            }
        }
        return tp;
    }

    bool moreData() override { return (ci < 2); }
    /** @brief check if the sparse array is sorted
    @return bool indicating sorted status
    */
    bool isSorted() const { return (sortCount == size()); }
    ValueT at(index_t rowN, index_t colN) const override
    {
        X cmp = key_computer.keyGen(rowN, colN);
        int I = block_computer.blockIndexGen(rowN, colN);
        if (isSorted()) {
            auto res = std::lower_bound(dVec[I].cbegin(),
                                        dVec[I].cend(),
                                        std::make_pair(cmp, 0),
                                        [](pLoc A, pLoc B) { return (A.first < B.first); });
            if (res == dVec[I].cend()) {
                return ValueT(0);
            }
            if (res->first == cmp) {
                return res->second;
            }
        } else {
            for (const auto& rv : dVec[I]) {
                if (rv.first == cmp) {
                    return rv.second;
                }
            }
        }
        return ValueT(0);
    }

  protected:
    class matrixIteratorSM {
      public:
        explicit matrixIteratorSM(const matrixDataSparseSMB<1, X, ValueT, M>* matrixData,
                                  index_t start = 0): mDS(matrixData)
        {
            if (start == 0) {
                ci = 0;

                cptr = mDS->dVec[0].cbegin();
                iend = mDS->dVec[0].cend();
            } else if (start == mDS->size()) {
                ci = 1;
                cptr = mDS->dVec[1].cend();
                iend = mDS->dVec[1].cend();
            }
        }

        matrixIteratorSM& operator++()
        {
            ++cptr;
            if (cptr == iend) {
                ++ci;
                if (ci < 2) {
                    cptr = mDS->dVec[1].cbegin();
                    iend = mDS->dVec[1].cend();
                }
            }
            return &this;
        }

        virtual matrixElement<ValueT> operator*() const override
        {
            return {mDS->key_computer.row(cptr->first),
                    mDS->key_computer.col(cptr->first),
                    cptr->second};
        }

      private:
        const matrixDataSparseSMB<1, X, ValueT, M>* mDS = nullptr;
        cIterator cptr;  //!< ptr to the beginning of the sequence;
        cIterator iend;  //!< ptr to the end of the current sequence;
        int ci = 0;  //!< indicator of which vector of the array we are sequencing on;
    };
};
