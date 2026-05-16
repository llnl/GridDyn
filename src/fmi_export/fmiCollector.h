/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/measurement/collector.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::fmi {
class FmiCoordinator;

/** collector object to interface with an fmi output*/
class FmiCollector: public collector {
  protected:
    std::vector<index_t> mValueReferences;  //!< vector of fmi value references that match the data
    FmiCoordinator* mCoordinator = nullptr;  //!< pointer the fmi coordination object
  public:
    FmiCollector();
    explicit FmiCollector(const std::string& name);
    //~fmiCollector();

    virtual std::unique_ptr<collector> clone() const override;

    virtual void cloneTo(collector* collectorClone = nullptr) const override;
    virtual change_code trigger(coreTime time) override;

    void set(std::string_view param, double val) override;
    void set(std::string_view param, std::string_view val) override;

    virtual const std::string& getSinkName() const override;

    virtual coreObject* getOwner() const override;
    friend class FmiCoordinator;

  protected:
    virtual void dataPointAdded(const collectorPoint& collectorDataPoint) override;
};

}  // namespace griddyn::fmi
