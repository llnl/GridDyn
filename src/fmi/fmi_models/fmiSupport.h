/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <vector>

/** mode defining how to get partial derivatives for Jacobian calculations*/
enum class refMode_t : unsigned char {
    direct = 0,
    level1 = 1,
    level2 = 2,
    level3 = 3,
    level4 = 4,
    level5 = 5,
    level6 = 6,
    level7 = 7,
    level8 = 8,

};

/** data class for storing variable information for states and outputs*/
class vInfo {
  public:
    int varIndex = -1;  //!< the actual variable index in the fmi
    int index = -1;  //!< the local index into a matrix
    bool isState = false;  //!< defining if it is a state [for output only]
    refMode_t refMode =
        refMode_t::direct;  //!< the mode to use for computing the partial derivatives
    std::vector<int> inputDep;  //!< the inputs on which the calculation depends
    std::vector<int> stateDep;  //!< the states on which the calculation depends
};
