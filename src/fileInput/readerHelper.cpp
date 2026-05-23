/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "readerHelper.h"

#include "fileInput.h"
#include <filesystem>

// library for printf debug statements

#include <cmath>
namespace griddyn {

void processParamString(GridParameter& param, ReaderInfo& ReaderInformation)
{
    if (!param.stringType) {
        return;
    }
    const double interpretedValue = interpretString(param.strVal, ReaderInformation);
    if (!std::isnan(interpretedValue)) {
        param.value = interpretedValue;
        param.stringType = false;
    } else {
        // can't be interpreted as a number so do a last check for string redefinitions
        param.strVal = ReaderInformation.checkDefines(param.strVal);
    }
}

}  // namespace griddyn
