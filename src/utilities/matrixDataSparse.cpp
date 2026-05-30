/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "MatrixDataSparse.hpp"

#include "MatrixDataSparse_impl.hpp"
#include <complex>
#include <vector>

template class MatrixDataSparse<int>;
// template class MatrixDataSparse<float>;
template class MatrixDataSparse<double>;
// template class MatrixDataSparse<std::complex<float>>;
// template class MatrixDataSparse<std::complex<double>>;

// template std::vector<index_t> findMissing<int>(MatrixDataSparse<int> &md);
// template std::vector<index_t> findMissing<float> (MatrixDataSparse<float> &md);
template std::vector<index_t> findMissing<double>(MatrixDataSparse<double>& md);
// template std::vector<index_t>
// findMissing<std::complex<float>>(MatrixDataSparse<std::complex<float>> &md);
// template std::vector<index_t>
// findMissing<std::complex<double>>(MatrixDataSparse<std::complex<double>> &md);

// template std::vector<std::vector<index_t>>
// findRank<int>(MatrixDataSparse<int> &md);
// template std::vector<std::vector<index_t>> findRank<float> (MatrixDataSparse<float> &md);
template std::vector<std::vector<index_t>> findRank<double>(MatrixDataSparse<double>& md);
// template std::vector<std::vector<index_t>>
// findRank<std::complex<float>>(MatrixDataSparse<std::complex<float>> &md);
// template std::vector<std::vector<index_t>>
// findRank<std::complex<double>>(MatrixDataSparse<std::complex<double>> &md);
