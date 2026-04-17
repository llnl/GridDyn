/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "matrixDataSparse.hpp"

#include "matrixDataSparse_impl.hpp"
#include <complex>
#include <vector>

template class matrixDataSparse<int>;
// template class matrixDataSparse<float>;
template class matrixDataSparse<double>;
// template class matrixDataSparse<std::complex<float>>;
// template class matrixDataSparse<std::complex<double>>;

// template std::vector<index_t> findMissing<int>(matrixDataSparse<int> &md);
// template std::vector<index_t> findMissing<float> (matrixDataSparse<float> &md);
template std::vector<index_t> findMissing<double>(matrixDataSparse<double>& md);
// template std::vector<index_t>
// findMissing<std::complex<float>>(matrixDataSparse<std::complex<float>> &md);
// template std::vector<index_t>
// findMissing<std::complex<double>>(matrixDataSparse<std::complex<double>> &md);

// template std::vector<std::vector<index_t>>
// findRank<int>(matrixDataSparse<int> &md);
// template std::vector<std::vector<index_t>> findRank<float> (matrixDataSparse<float> &md);
template std::vector<std::vector<index_t>> findRank<double>(matrixDataSparse<double>& md);
// template std::vector<std::vector<index_t>>
// findRank<std::complex<float>>(matrixDataSparse<std::complex<float>> &md);
// template std::vector<std::vector<index_t>>
// findRank<std::complex<double>>(matrixDataSparse<std::complex<double>> &md);
