/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "readerHelper.h"

#include "fileInput.h"

#include <filesystem>

// library for printf debug statements

#include <cmath>
namespace griddyn {
using namespace readerConfig;

void paramStringProcess(gridParameter& param, readerInfo& ri)
{
    if (!param.stringType) {
        return;
    }
    double val = interpretString(param.strVal, ri);
    if (!(std::isnan(val))) {
        param.value = val;
        param.stringType = false;
    } else {
        // can't be interpreted as a number so do a last check for string redefinitions
        param.strVal = ri.checkDefines(param.strVal);
    }
}

}  // namespace griddyn
