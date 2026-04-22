/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/measurement/collector.h"
#include <memory>
#include <string>

class dimeClientInterface;

namespace griddyn::dimeLib {
class dimeCollector: public collector {
  private:
    std::string server;
    std::string processName;
    std::unique_ptr<dimeClientInterface> dime;

  public:
    dimeCollector(coreTime time0 = timeZero, coreTime period = timeOneSecond);
    explicit dimeCollector(const std::string& name);
    ~dimeCollector();

    virtual std::unique_ptr<collector> clone() const override;

    virtual void cloneTo(collector* col) const override;
    virtual change_code trigger(coreTime time) override;

    void set(std::string_view param, double val) override;
    void set(std::string_view param, std::string_view val) override;

    virtual const std::string& getSinkName() const override;
};

}  // namespace griddyn::dimeLib

