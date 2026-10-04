/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "IEELParameters.h"
#include "LoadFactory.h"
#include "WSCCParameters.h"

namespace griddyn::loads {
/** Whether an existing load can safely be replaced by a static characteristic. */
bool supportsCharacteristicReplacement(const GridLoad& load);

/** Build a generic load-template factory for PSS/E's all-load IEEL record. */
LoadTemplateFactory makeIEELALLoadTemplate(IEELParameters parameters);

/** Build a generic load-template factory for a scoped PSLF WSCC record. */
LoadTemplateFactory makeWSCCLoadTemplate(WSCCParameters parameters);
}  // namespace griddyn::loads
