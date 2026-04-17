#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "fmiSupport.h"
#include "griddyn/gridSubModel.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class fmi2CoSimObject;
class outputEstimator;

namespace griddyn::fmi {
    /** class defining a subModel interacting with an FMU v2.0 object using cosimulation*/
    class fmiCoSimSubModel: public gridSubModel {
      public:
        enum fmiSubModelFlags {
            use_output_estimator = object_flag2,
            fixed_output_interval = object_flag3,
            has_derivative_function = object_flag5,
        };

      protected:
        std::shared_ptr<fmi2CoSimObject> cs;

        std::vector<outputEstimator*> estimators;  //!< vector of objects used for output estimation
        double localIntegrationTime = 0.01;

      private:
        int lastSeqID = 0;

      public:
        fmiCoSimSubModel(const std::string& newName = "fmicosimsubmodel_#",
                         std::shared_ptr<fmi2CoSimObject> fmi = nullptr);

        fmiCoSimSubModel(std::shared_ptr<fmi2CoSimObject> fmi = nullptr);
        virtual ~fmiCoSimSubModel();
        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void dynObjectInitializeA(coreTime time, std::uint32_t flags) override;
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;
        virtual stringVec getOutputNames() const;
        virtual stringVec getInputNames() const;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual double get(const std::string& param,
                           units::unit unitType = units::defunit) const override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
        virtual void ioPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode) override;

        IOdata getOutputs(const IOdata& inputs,
                          const stateData& sD,
                          const solverMode& sMode) const override;
        virtual double getDoutdt(const IOdata& inputs,
                                 const stateData& sD,
                                 const solverMode& sMode,
                                 index_t outputNum = 0) const override;
        virtual double getOutput(const IOdata& inputs,
                                 const stateData& sD,
                                 const solverMode& sMode,
                                 index_t outputNum = 0) const override;

        virtual double getOutput(index_t outputNum = 0) const override;

        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;
        bool isLoaded() const;

      protected:
        void loadFMU();

        void instantiateFMU();
        void makeSettableState();
        void resetState();
        double getPartial(int depIndex, int refIndex, refMode_t mode);

        void loadOutputJac(int index = -1);
    };

}  // namespace griddyn::fmi
