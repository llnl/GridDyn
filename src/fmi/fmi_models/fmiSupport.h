/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <vector>

/** mode defining how to get partial derivatives for Jacobian calculations*/
enum class RefMode : unsigned char {
    DIRECT = 0,
    LEVEL1 = 1,
    LEVEL2 = 2,
    LEVEL3 = 3,
    LEVEL4 = 4,
    LEVEL5 = 5,
    LEVEL6 = 6,
    LEVEL7 = 7,
    LEVEL8 = 8,

};

/** data class for storing variable information for states and outputs*/
class ValueDependencyInfo {
  public:
    int varIndex = -1;  //!< the actual variable index in the fmi
    int index = -1;  //!< the local index into a matrix
    bool isState = false;  //!< defining if it is a state [for output only]
    RefMode refMode = RefMode::DIRECT;  //!< the mode to use for computing the partial derivatives
    std::vector<int> inputDep;  //!< the inputs on which the calculation depends
    std::vector<int> stateDep;  //!< the states on which the calculation depends
};
