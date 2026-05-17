/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiSupport.h"
#include "griddyn/gridSubModel.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class Fmi2CoSimObject;
class OutputEstimator;

namespace griddyn::fmi {
/** class defining a subModel interacting with an FMU v2.0 object using cosimulation*/
class FmiCoSimSubModel: public gridSubModel {
  public:
    enum fmiSubModelFlags {
        use_output_estimator = object_flag2,
        fixed_output_interval = object_flag3,
        has_derivative_function = object_flag5,
    };

  protected:
    std::shared_ptr<Fmi2CoSimObject> cs;

    std::vector<OutputEstimator*> estimators;  //!< vector of objects used for output estimation
    double localIntegrationTime = 0.01;

  private:
    int lastSeqID = 0;

  public:
    FmiCoSimSubModel(const std::string& newName = "fmicosimsubmodel_#",
                     std::shared_ptr<Fmi2CoSimObject> fmi = nullptr);

    FmiCoSimSubModel(std::shared_ptr<Fmi2CoSimObject> fmi = nullptr);
    virtual ~FmiCoSimSubModel();
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(coreTime time, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;
    virtual stringVec getOutputNames() const;
    virtual stringVec getInputNames() const;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
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

    virtual void updateLocalCache([[maybe_unused]] const IOdata& inputs,
                                  [[maybe_unused]] const stateData& sD,
                                  [[maybe_unused]] const solverMode& sMode) override;
    bool isLoaded() const;

  protected:
    void loadFmu();

    void instantiateFMU();
    void makeSettableState();
    void resetState();
    double getPartial(int depIndex, int refIndex, RefMode mode);

    void loadOutputJac(int index = -1);
};

}  // namespace griddyn::fmi

