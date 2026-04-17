#pragma once

/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#include "gmlc/containers/WorkQueue.hpp"
#include <memory>

namespace griddyn {
/** get a copy of a pointer to a global work queue
the work queue itself is thread safe
*/
const std::shared_ptr<gmlc::containers::WorkQueue>& getGlobalWorkQueue(int threads = -1);
}  // namespace griddyn
