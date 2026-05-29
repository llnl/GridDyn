/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridDynDefinitions.hpp"

#define SCHEDULER_UPDATE 1501

namespace griddyn {
class Tsched {
  public:
    coreTime time = maxTime;
    double target = 0;
    Tsched() {}
    Tsched(coreTime atime, double atarget): time(atime), target(atarget) {}
};

// comparison operators for Tsched classes
bool operator<(const Tsched& td1, const Tsched& td2);

bool operator<=(const Tsched& td1, const Tsched& td2);

bool operator>(const Tsched& td1, const Tsched& td2);

bool operator>=(const Tsched& td1, const Tsched& td2);

bool operator==(const Tsched& td1, const Tsched& td2);

bool operator!=(const Tsched& td1, const Tsched& td2);

bool operator<(const Tsched& td1, coreTime timeC);

bool operator<=(const Tsched& td1, coreTime timeC);

bool operator>(const Tsched& td1, coreTime timeC);

bool operator>=(const Tsched& td1, coreTime timeC);

bool operator==(const Tsched& td1, coreTime timeC);

bool operator!=(const Tsched& td1, coreTime timeC);

}  // namespace griddyn
