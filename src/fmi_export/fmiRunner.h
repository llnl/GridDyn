/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "FMI2/fmi2FunctionTypes.h"
#include "core/coreOwningPtr.hpp"
#include "runner/gridDynRunner.h"
#include <bitset>
#include <future>
#include <string>

namespace griddyn {
class readerInfo;
namespace fmi {
    class fmiCoordinator;

    /** fmiRunner is the execution object for executing under an fmi context
it inherits from gridDynRunner and adds some extra features necessary for executing under an fMI
*/
    class fmiRunner: public GriddynRunner {
      private:
        coreOwningPtr<fmiCoordinator> coord;  //!< the coordinator object for managing object that
                                              //!< manage the fmi inputs and outputs
        std::bitset<7> loggingCategories;  //!< indicators of which logging categories to use
        bool runAsync_ = false;  //!< indicator that we should run asynchronously
        bool modelExchangeRunner =
            false;  //!< indicator that the object is running in model exchange mode
        std::future<void>
            async_retFMI;  //!< the future object corresponding to the asyncrhonous operation
      public:
        /** construct an fmurunner object
    @param name the name of the runner
    @param resourceLocations the FMU resource location information
    @param function a set of helper function from the FMI master
    @param ModelExchange set to true if this is instantiating a model exchange object (optional
    defaults to false)
    */
        fmiRunner(const std::string& name,
                  const std::string& resourceLocations,
                  const fmi2CallbackFunctions* functions,
                  bool ModelExchange = false);
        ~fmiRunner();

      public:
        virtual coreTime Run() override;

        /** update the FMI outputs*/
        void UpdateOutputs();

        virtual coreTime Step(coreTime time) override;
        virtual void StepAsync(coreTime time) override;
        virtual void Finalize() override;

      private:
        using GriddynRunner::Reset;

      public:
        virtual int Reset() override;

        id_type_t GetID() const;

        virtual bool Set(index_t vr, double val);
        virtual bool SetString(index_t vr, const char* s);
        virtual double Get(index_t vr);

        void setLoggingCategories(std::bitset<7> logCat) { loggingCategories = logCat; }
        /** check whether the runner is set to run asynchronously*/
        bool runAsynchronously() const { return runAsync_; }
        /** set the asyncrhonous mode for operation*/
        void setAsynchronousMode(bool async)
        {
            runAsync_ = (stepFinished != nullptr) ? async : false;
        }

        /** return true if the object is a model exchange object*/
        bool isModelExchangeObject() const { return modelExchangeRunner; }
        /** check whether an asynchronous step call is finished*/
        bool isFinished() const;
        /** locate a value reference from a name*/
        index_t findVR(const std::string& varName) const;

        void logger(int level, const std::string& logMessage);

      private:
        // these are used for logging
        fmi2CallbackLogger loggerFunc = nullptr;  // reference to the logger function
        fmi2StepFinished stepFinished = nullptr;  //!< reference to a step finished function
      public:
        fmi2Component fmiComp;
        std::string identifier;
        std::string recordDirectory;
        std::string resource_loc;
    };

}  // namespace fmi
}  // namespace griddyn
