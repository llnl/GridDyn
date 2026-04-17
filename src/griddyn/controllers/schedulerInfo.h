#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gridDynDefinitions.hpp"

#define SCHEDULER_UPDATE 1501

namespace griddyn {
class tsched {
  public:
    coreTime time = maxTime;
    double target = 0;
    tsched() {}
    tsched(coreTime atime, double atarget): time(atime), target(atarget) {}
};

// comparison operators for tsched classes
bool operator<(const tsched& td1, const tsched& td2);

bool operator<=(const tsched& td1, const tsched& td2);

bool operator>(const tsched& td1, const tsched& td2);

bool operator>=(const tsched& td1, const tsched& td2);

bool operator==(const tsched& td1, const tsched& td2);

bool operator!=(const tsched& td1, const tsched& td2);

bool operator<(const tsched& td1, coreTime timeC);

bool operator<=(const tsched& td1, coreTime timeC);

bool operator>(const tsched& td1, coreTime timeC);

bool operator>=(const tsched& td1, coreTime timeC);

bool operator==(const tsched& td1, coreTime timeC);

bool operator!=(const tsched& td1, coreTime timeC);

}  // namespace griddyn
