/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/measurement/collector.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
namespace fmi {
    class fmiCoordinator;

    /** collector object to interface with an fmi output*/
    class fmiCollector: public collector {
      protected:
        std::vector<index_t> vrs;  //!< vector of fmi value references that match the data
        fmiCoordinator* coord = nullptr;  //!< pointer the fmi coordination object
      public:
        fmiCollector();
        explicit fmiCollector(const std::string& name);
        //~fmiCollector();

        virtual std::unique_ptr<collector> clone() const override;

        virtual void cloneTo(collector* gr = nullptr) const override;
        virtual change_code trigger(coreTime time) override;

        void set(const std::string& param, double val) override;
        void set(const std::string& param, const std::string& val) override;

        virtual const std::string& getSinkName() const override;

        virtual coreObject* getOwner() const override;
        friend class fmiCoordinator;

      protected:
        virtual void dataPointAdded(const collectorPoint& cp) override;
    };

}  // namespace fmi
}  // namespace griddyn
