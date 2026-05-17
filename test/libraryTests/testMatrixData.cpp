/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

// test case for CoreObject object

#include "../gtestHelper.h"
#include "griddyn/gridDynDefinitions.hpp"
#include "utilities/matrixDataSparse.hpp"
#include "utilities/matrixDataSparseSM.hpp"
#include "utilities/matrixOps.h"
#include <gtest/gtest.h>
#include <iostream>
#include <random>
#include <vector>

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(MatrixDataTests, BlockCompute)
{
    blockCompute<2, SparseOrdering::COLUMN_ORDERED> bc1;
    bc1.setMaxIndex(0, 20);
    std::vector<index_t> colcnt(6, 0);
    for (index_t pp = 0; pp < 20; ++pp) {
        ++colcnt[bc1.blockIndexGen(0, pp)];
    }
    EXPECT_EQ(colcnt[0], 2);
    EXPECT_EQ(colcnt[1], 8);
    EXPECT_EQ(colcnt[2], 8);
    EXPECT_EQ(colcnt[3], 2);

    blockCompute<2, SparseOrdering::ROW_ORDERED> bc2;
    bc2.setMaxIndex(20, 0);
    std::vector<index_t> colcnt2(6, 0);
    for (index_t pp = 0; pp < 20; ++pp) {
        ++colcnt2[bc2.blockIndexGen(pp, 0)];
    }
    EXPECT_EQ(colcnt2[0], 2);
    EXPECT_EQ(colcnt2[1], 8);
    EXPECT_EQ(colcnt2[2], 8);
    EXPECT_EQ(colcnt2[3], 2);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(MatrixDataTests, BlockCompute2)
{
    blockCompute<3, SparseOrdering::COLUMN_ORDERED> bc1;
    bc1.setMaxIndex(7893, 7893);
    std::vector<index_t> colcnt(10, 0);
    for (index_t pp = 0; pp < 7893; ++pp) {
        ++colcnt[bc1.blockIndexGen(pp, pp)];
    }
    EXPECT_EQ(colcnt[0], 874);
    EXPECT_EQ(colcnt[1], 1024);
    EXPECT_EQ(colcnt[2], 1024);
    EXPECT_EQ(colcnt[3], 1024);
    EXPECT_EQ(colcnt[4], 1024);
    EXPECT_EQ(colcnt[5], 1024);
    EXPECT_EQ(colcnt[6], 1024);
    EXPECT_EQ(colcnt[7], 875);
    EXPECT_EQ(colcnt[8], 0);
}

TEST(MatrixDataTests, Keygen)
{
    auto key1 = keyCompute<std::uint32_t, SparseOrdering::COLUMN_ORDERED>::keyGen(45, 1);
    EXPECT_EQ(key1, (1 << 16) + 45);
    auto key2 = keyCompute<std::uint32_t, SparseOrdering::ROW_ORDERED>::keyGen(45, 1);
    EXPECT_EQ(key2, (45 << 16) + 1);

    keyCompute<std::uint32_t, SparseOrdering::COLUMN_ORDERED> kc1;
    keyCompute<std::uint32_t, SparseOrdering::ROW_ORDERED> kc2;
    EXPECT_EQ(kc1.row(key1), 45);
    EXPECT_EQ(kc1.col(key1), 1);
    EXPECT_EQ(kc2.row(key2), 45);
    EXPECT_EQ(kc2.col(key2), 1);

    auto key3 = keyCompute<std::uint64_t, SparseOrdering::COLUMN_ORDERED>::keyGen(45, 1);
    EXPECT_EQ(key3, (static_cast<std::uint64_t>(1) << 32) + 45);
    auto key4 = keyCompute<std::uint64_t, SparseOrdering::ROW_ORDERED>::keyGen(45, 1);
    EXPECT_EQ(key4, (static_cast<std::uint64_t>(45) << 32) + 1);

    keyCompute<std::uint64_t, SparseOrdering::COLUMN_ORDERED> kc3;
    keyCompute<std::uint64_t, SparseOrdering::ROW_ORDERED> kc4;
    EXPECT_EQ(kc3.row(key3), 45);
    EXPECT_EQ(kc3.col(key3), 1);
    EXPECT_EQ(kc4.row(key4), 45);
    EXPECT_EQ(kc4.col(key4), 1);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(MatrixDataTests, Matrix1)
{
    matrixDataSparseSMB<4, std::uint64_t> bigMatrix;
    bigMatrix.setColLimit(1000000);
    bigMatrix.setRowLimit(1000000);
    bigMatrix.reserve(200000);

    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::default_random_engine generator(12345);
    std::uniform_int_distribution<std::uint32_t> distribution(1, 999998);
    for (size_t pp = 0; pp < 199997; ++pp) {
        auto index1 = distribution(generator);
        auto index2 = distribution(generator);
        bigMatrix.assign(index1, index2, 1.0);
    }
    bigMatrix.assign(0, 0, 3.27);
    bigMatrix.assign(999999, 999999, 6.129);

    bigMatrix.compact();

    bigMatrix.start();

    auto entry = bigMatrix.next();
    EXPECT_EQ(entry.col, 0);
    EXPECT_EQ(entry.row, 0);
    EXPECT_DOUBLE_EQ(entry.data, 3.27);
    auto pcol = entry.col;
    for (index_t pp = 1; pp < bigMatrix.size(); ++pp) {
        entry = bigMatrix.next();
        if (entry.col < pcol) {
            EXPECT_LT(entry.col, pcol);
        }
        pcol = entry.col;
    }
    EXPECT_EQ(entry.col, 999999);
    EXPECT_EQ(entry.row, 999999);
    EXPECT_DOUBLE_EQ(entry.data, 6.129);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(MatrixDataTests, Matrix2)
{
    matrixDataSparseSMB<3, std::uint64_t, double, SparseOrdering::ROW_ORDERED> bigMatrix;
    bigMatrix.setColLimit(1000000);
    bigMatrix.setRowLimit(1000000);
    bigMatrix.reserve(200000);

    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::default_random_engine generator(12345);
    std::uniform_int_distribution<std::uint32_t> distribution(1, 999998);
    for (size_t pp = 0; pp < 199997; ++pp) {
        auto index1 = distribution(generator);
        auto index2 = distribution(generator);
        bigMatrix.assign(index1, index2, 1.0);
    }
    bigMatrix.assign(0, 0, 3.27);
    bigMatrix.assign(999999, 999999, 6.129);

    bigMatrix.compact();

    bigMatrix.start();

    auto entry = bigMatrix.next();
    EXPECT_EQ(entry.col, 0);
    EXPECT_EQ(entry.row, 0);
    EXPECT_DOUBLE_EQ(entry.data, 3.27);
    auto prow = entry.row;
    for (index_t pp = 1; pp < bigMatrix.size(); ++pp) {
        entry = bigMatrix.next();
        if (entry.row < prow) {
            EXPECT_LT(entry.row, prow);
        }
        prow = entry.row;
    }
    EXPECT_EQ(entry.col, 999999);
    EXPECT_EQ(entry.row, 999999);
    EXPECT_DOUBLE_EQ(entry.data, 6.129);
}

TEST(MatrixDataTests, SparseMatrix)
{
    matrixDataSparse<double> testMatrix;
    testMatrix.setColLimit(10);
    testMatrix.setRowLimit(10);
    testMatrix.assign(0, 0, 3.1);
    testMatrix.assign(6, 7, 2.1);
    testMatrix.assign(6, 7, 2.1);
    testMatrix.assign(3, 3, 5.1);

    testMatrix.compact();
    // should have compacted the two 6,7 element
    EXPECT_EQ(testMatrix.size(), 3_ind);

    auto itend = testMatrix.end();
    auto itbegin = testMatrix.begin();
    auto matrixEntry = *itbegin;
    EXPECT_NEAR(matrixEntry.data, 3.1, 1e-12);
    ++itbegin;
    matrixEntry = *itbegin;
    EXPECT_NEAR(matrixEntry.data, 5.1, 1e-12);
    auto it2 = itbegin++;
    matrixEntry = *itbegin;
    EXPECT_NEAR(matrixEntry.data, 4.2, 1e-12);
    EXPECT_NEAR((*it2).data, 5.1, 1e-12);
    ++itbegin;
    EXPECT_EQ(itbegin, itend);
}

TEST(MatrixDataTests, SparseMatrixMultiply)
{
    matrixDataSparse<double> testMatrix;
    testMatrix.setColLimit(10);
    testMatrix.setRowLimit(10);
    for (int ii = 0; ii < 10; ++ii) {
        testMatrix.assign(ii, ii, static_cast<double>(ii));
    }

    std::vector<double> values(10, 1.0);
    auto res = matrixDataMultiply(testMatrix, values.data());
    EXPECT_EQ(res.size(), 10U);

    int ecount = 0;
    for (int kk = 0; kk < 10; ++kk) {
        if (std::abs(res[kk] - static_cast<double>(kk)) > 0.000000001) {
            ++ecount;
        }
    }
    EXPECT_EQ(ecount, 0);
}
