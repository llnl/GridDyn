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

#include "utilities/matrixData.hpp"

#include <boost/numeric/ublas/matrix_sparse.hpp>

/** @brief class implementing an expandable sparse matrix based on the boost matrices geared for
 * Jacobian entries*/
template<class type>
class matrixDataBoost: public matrixData<type> {
  public:
    boost::numeric::ublas::mapped_matrix<type, boost::numeric::ublas::column_major>
        m;  //!< boost matrix to interface
  public:
    /** @brief constructor
    @param[in] R  number of rows
    @param[in] C  number of columns
    */
    matrixDataBoost(count_t R, count_t C): matrixData<type>(R, C), m(R, C) {}
    /**
     * function to clear the data
     */
    void clear() override { m.clear(); }
    void assign(index_t X, index_t Y, const type& num) override
    {
        m(X, Y) += num;
        // printf("insert [%d,%d]=%f\n", X, Y, num);
    }

    count_t size() const override { return static_cast<count_t>(m.nnz()); }
    count_t capacity() const override { return m.nnz_capacity(); }
    /**
     * @brief don't use this function
     */
    matrixElement<type> element(index_t N) { return matrixElement<type>(); }
    /**
     * @brief don't use this function
     */
    matrixElement<type> start() override
    {
        return {matrixData<type>::rowLim, matrixData<type>::colLim, type(0)};
    }
    /**
     * @brief don't use this function
     */
    matrixElement<type> next() override
    {
        return {matrixData<type>::rowLim, matrixData<type>::colLim, type(0)};
    }
    type at(index_t rowN, index_t colN) const override { return m(rowN, colN); }
    auto begin() const { return matriIteratorBoost(this, 0); }
    auto end() const { return matriIteratorBoost(this, size()); }

  protected:
    class matrixIteratorBoost {
      public:
        explicit matrixIteratorBoost(const matrixDataBoost<type>* matrixData, index_t start = 0)
        {
            if (start == 0) {
                cptr = mDB->m.begin1();
                if (cptr == mDB->m.end1()) {
                    return;
                }
                cptr2 = cptr.begin();
                cptr2end = cptr.end();
                while (cptr2 == cptr2end) {
                    ++cptr;
                    if (cptr == mDB->m.end1()) {
                        return;
                    }
                    cptr2 = cptr.begin();
                    cptr2end = cptr.end();
                }
            } else if (start == mDB->size()) {
                cptr = mDB->m.end1();
            }
        }

        matrixIteratorBoost& operator++() override
        {
            ++cptr2;
            if (cptr2 == cptr2end) {
                ++cptr;
                cptr2 = cptr.begin();
                cptr2end = cptr.end();
                while (cptr2 == cptr2end) {
                    ++cptr;
                    if (cptr == mDB->m.end1()) {
                        return;
                    }
                    cptr2 = cptr.begin();
                    cptr2end = cptr.end();
                }
            }
        }

        virtual matrixElement<type> operator*() const override
        {
            return {cptr2.index1(), cptr2.index2(), *cptr2};
        }

      private:
        matrixDataBoost<type>* mDB = nullptr;
        decltype(mDB->m.begin1()) cptr;
        decltype(cptr->begin()) cptr2;
        decltype(cptr->end()) cptr2end;
    };
};
