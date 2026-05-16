/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../core/coreOwningPtr.hpp"
#include "../runner/gridDynRunner.h"
#include <memory>

namespace helics {
class Federate;
}

namespace griddyn {
class readerInfo;
namespace helicsLib {
    class HelicsCoordinator;

    /** helicsRunner is the execution object for executing in coordination with the Helics
co-simulation environment it inherits from gridDynRunner and adds some extra features necessary for
executing with helics
*/
    class HelicsRunner: public GriddynRunner {
      private:
        coreOwningPtr<HelicsCoordinator> coord_;  //!< the coordinator object for managing object
                                                  //!< that manage the HELICS coordination
        std::shared_ptr<helics::Federate> fed_;  //!< pointer to the helics federate object
      public:
        HelicsRunner();
        explicit HelicsRunner(std::shared_ptr<gridDynSimulation> sim);
        ~HelicsRunner();

      public:
        virtual std::shared_ptr<CLI::App> generateLocalCommandLineParser(readerInfo& ri) override;

        virtual void simInitialize() override;
        virtual coreTime Run() override;

        virtual coreTime Step(coreTime time) override;

        virtual void Finalize() override;
    };

}  // namespace helicsLib
}  // namespace griddyn
