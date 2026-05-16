/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreOwningPtr.hpp"
#include "runner/gridDynRunner.h"
#include <memory>

namespace griddyn {
class readerInfo;
namespace dimeLib {
    /** DimeRunner is the execution object for executing in coordination with the Dime co-simulation
environment it inherits from gridDynRunner and adds some extra features necessary for executing with
dime
*/
    class DimeRunner: public GriddynRunner {
      private:
        // coreOwningPtr<dimeCoordinator> coord_; //!< the coordinator object for managing object
        // that manage the HELICS coordination
      public:
        DimeRunner();
        DimeRunner(std::shared_ptr<gridDynSimulation> sim);
        ~DimeRunner();

      public:
        virtual std::shared_ptr<CLI::App>
            generateLocalCommandLineParser(readerInfo& readerInformation) override;

        virtual coreTime Run(void) override;

        virtual coreTime Step(coreTime time) override;

        virtual void Finalize(void) override;
    };

}  // namespace dimeLib
}  // namespace griddyn
