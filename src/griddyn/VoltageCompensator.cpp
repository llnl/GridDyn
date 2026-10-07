/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "VoltageCompensator.h"

#include "core/CoreObjectTemplates.hpp"
#include <string>
#include <vector>

namespace griddyn {
VoltageCompensator::VoltageCompensator(const std::string& objName): GridSubModel(objName)
{
    m_inputSize = voltageCompensatorInputCount;
    m_outputSize = 1;
}

CoreObject* VoltageCompensator::clone(CoreObject* obj) const
{
    auto* compensatorClone = cloneBase<VoltageCompensator, GridSubModel>(this, obj);
    return (compensatorClone != nullptr) ? compensatorClone : obj;
}

const std::vector<stringVec>& VoltageCompensator::inputNames() const
{
    static const std::vector<stringVec> names{{"voltage"},
                                              {"id"},
                                              {"iq"},
                                              {"vd"},
                                              {"vq"},
                                              {"electrical_power"},
                                              {"electrical_torque"},
                                              {"xad_ifd"}};
    return names;
}

const std::vector<stringVec>& VoltageCompensator::outputNames() const
{
    static const std::vector<stringVec> names{{"vct", "voltage_compensated"}};
    return names;
}
}  // namespace griddyn
