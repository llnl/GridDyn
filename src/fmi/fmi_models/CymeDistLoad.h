/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiMELoad3phase.h"
#include <string>

namespace griddyn::fmi {
class CymeDistLoadME: public FmiMELoad3phase {
    static std::atomic<int> indexCounter;
    std::string configFile;
    index_t configIndex = 0;

  public:
    CymeDistLoadME(const std::string& objName = "cymedist_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

  private:
    void loadConfigFile(const std::string& configFileName);
};

}  // namespace griddyn::fmi
