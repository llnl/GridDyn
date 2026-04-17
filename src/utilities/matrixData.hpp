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

#include "indexTypes.hpp"
#include <iterator>
#include <utility>

/** @brief convenience structure for returning data
 */
template<class ValueT>
class matrixElement {
  public:
    using value_t = ValueT;

    index_t row;  //!< row
    index_t col;  //!< column
    value_t data;  //!< value
    matrixElement() = default;
    matrixElement(index_t R, index_t C, value_t D) noexcept: row{R}, col{C}, data{std::move(D)} {}
};

template<class X>
static bool compareRow(const matrixElement<X>& A, const matrixElement<X>& B)
{
    return (A.row < B.row) ? true : ((A.row > B.row) ? false : (A.col < B.col));
}
template<class X>
static bool compareCol(const matrixElement<X>& A, const matrixElement<X>& B)
{
    return (A.col < B.col) ? true : ((A.col > B.col) ? false : (A.row < B.row));
}

/** @brief class implementing a matrix entry class for handling Jacobian entries
 *
 *  @details this is a purely virtual interface a specific
 *  instantiation is required in general matrixData objects are not
 *  thread safe, accessing from more than one thread at a time will
 *  likely be problematic, because they are virtual classes, no
 *  specific iterator is possible without dynamic allocation and that
 *  makes them really slow.  there is a sequential access methods,
 *  start(), next(), and moreData(), the element method should be
 *  thread-safe for random access but is likely to be slow in many
 *  implementations. Specific implementations may define an iterator
 *  that may be more useful. individual classes
 */
template<class ValueT = double>
class matrixData {
  public:
    using value_t = ValueT;

  public:
    /** @brief constructor
     *  @param[in] rows  the initial row limit
     *  @param[in] cols the initial column limit
     */
    explicit matrixData(index_t rows = kIndexMax, index_t cols = kIndexMax) noexcept:
        rowLim{rows}, colLim{cols}
    {
    }

    /** @brief virtual destructor */
    virtual ~matrixData() = default;

    /** @brief function to clear the data */
    virtual void clear() = 0;

    /**
     *  @brief add a new Jacobian element
     *  @param[in] row the row of the element
     *  @param[in] col the column of the element
     *  @param[in] num the value of the element
     */
    virtual void assign(index_t row, index_t col, value_t num) = 0;

    /** @brief assign with a check on the row
     *  add a new Jacobian element if the arguments are valid (row<rowNum)
     *  @param[in] row the row of the element
     *  @param[in] col the column of the element
     *  @param[in] num the value of the element
     */
    void assignCheckRow(index_t row, index_t col, value_t num)
    {
        if (isValidIndex(row, rowLim)) {
            assign(row, col, num);
        }
    }

    /** @brief assign with a check on the col
     *  add a new Jacobian element if the arguments are valid (col<colLim)
     *  @param[in] row the row of the element
     *  @param[in] col the column of the element
     *  @param[in] num the value of the element
     */
    void assignCheckCol(index_t row, index_t col, value_t num)
    {
        if (isValidIndex(col, colLim)) {
            assign(row, col, num);
        }
    }

    /**
     *  @brief assign with a check on the row and column
     *
     *  Add a new Jacobian element if the arguments are valid
     *  (X<rowLim) &&(Y<colLim)
     *
     *  @param[in] row the row index in the matrix
     *  @param[in] col the column index in the matrix
     *  @param[in] num the value of the element
     */
    void assignCheck(index_t row, index_t col, value_t num)
    {
        if ((isValidIndex(col, colLim)) && (isValidIndex(row, rowLim))) {
            assign(row, col, num);
        }
    }

    /**
     *  @brief get the maximum row number
     *  @return the max row value
     */
    count_t rowLimit() const { return rowLim; }
    /**
     *  @brief get the maximum row number
     *  @return the max col value
     */
    count_t colLimit() const { return colLim; }

  private:
    /**
     *  @brief private function for derived classes to notify of a limitUpdate
     *  @param[in] newRowLimit the new row limit
     *  @param[in] newColLimit the new column limit
     */
    virtual void limitUpdate(index_t newRowLimit, index_t newColLimit)
    {
        (void)(newRowLimit), void(newColLimit);
    }
    /**
     *  @brief get the number of points
     *  @return the number of points
     */
  public:
    /**
     *  @brief set the maximum row count
     *  @param[in] limit the new row limit
     */
    void setRowLimit(index_t limit)
    {
        rowLim = limit;
        limitUpdate(rowLim, colLim);
    };
    /**
     *  @brief set the maximum number of columns
     *  @param[in] limit the new column limit
     */
    void setColLimit(index_t limit)
    {
        colLim = limit;
        limitUpdate(rowLim, colLim);
    };

    virtual count_t size() const = 0;
    /**
     *  @brief get the current capacity
     *  @return the maximum number of points
     */
    virtual count_t capacity() const = 0;

    /**
     *  @brief reserve space in the array for
     */
    virtual void reserve(count_t /*maxNonZeros*/) {}

    /**
     *  @brief get the value at a specific location
     *  @param[in] rowN  the row number
     *  @param[in] colN  the column number
     */
    virtual value_t at(index_t rowN, index_t colN) const = 0;

    /**
     *  @brief get element at index N
     *  @param[in] N the element number to return
     *  @return the element corresponding to the index
     */
    virtual matrixElement<value_t> element(index_t N) const = 0;

    /**  @brief change the matrixData to a compact sorted form
     */
    virtual void compact()
    {
        // many arrays would already be in compact form so this should
        // do nothing in the default case
    }

    /**  @brief set the array to begin a sequence of retrievals
     */
    virtual void start() { cur = 0; }
    /**
     *  @brief gets the next data triple
     *
     *  if called after all the elements have been retrieved should
     *  return invalid row and column index;
     *
     *  @return a triple with the row/col/val of the first element
     */
    virtual matrixElement<value_t> next() { return element(cur++); }
    /**
     *  @brief check if the data sequence is at its end
     *  @return true if there are more points to grab false if not
     */
    virtual bool moreData() { return (cur < size()); }
    /**
     *  @brief merge 2 matrixData structures together
     *  @param[in] a2 the matrixData to merge in
     */
    virtual void merge(matrixData<value_t>& a2)
    {
        count_t count = a2.size();
        a2.start();
        for (index_t nn = 0; nn < count; ++nn) {
            auto tp = a2.next();
            assign(tp.row, tp.col, tp.data);
        }
    }

    /**
     *  @brief merge 2 matrixData structures together
     *
     *  @param[in] a2 the matrixData to merge in
     *  @param[in] scale a double scaler for each of the elements in
     *  the second matrix;
     */
    virtual void merge(matrixData<value_t>& a2, value_t scale)
    {
        count_t count = a2.size();
        a2.start();
        for (index_t nn = 0; nn < count; ++nn) {
            auto tp = a2.next();
            assign(tp.row, tp.col, tp.data * scale);
        }
    }
    /**
     *  @merge copy and translate a row from a2 into the calling matrixData
     *  @param[in] a2 the matrixData to  copy and translate
     *  @param[in] origRow  the original row
     *  @param[in] newRow the new row Value
     */
    virtual void copyTranslateRow(matrixData<value_t>& a2, index_t origRow, index_t newRow)
    {
        count_t count = a2.size();
        a2.start();
        for (index_t nn = 0; nn < count; ++nn) {
            auto nextElement = a2.next();
            if (nextElement.row == origRow) {
                assign(newRow, nextElement.col, nextElement.data);
            }
        }
    }

  private:
    index_t rowLim;  //!< the maximum row index
    index_t colLim;  //!< the maximum column index
  protected:
    index_t cur = 0;  //!< the current element location for a next operation

  private:
    inline bool isValidIndex(index_t index, index_t imax)
    {
#ifdef UNSIGNED_INDEXING
        return (index < imax);
#else
        return ((index >= 0) && (index < imax));
#endif
    }
};
