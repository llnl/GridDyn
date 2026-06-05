/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fmiSupport.h"
#include "griddyn/GridSubModel.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class Fmi2CoSimObject;
class OutputEstimator;

namespace griddyn::fmi {
/** class defining a subModel interacting with an FMU v2.0 object using cosimulation*/
class FmiCoSimSubModel: public GridSubModel {
  public:
    enum FmiSubModelFlags {
        USE_OUTPUT_ESTIMATOR = OBJECT_FLAG2,
        FIXED_OUTPUT_INTERVAL = OBJECT_FLAG3,
        HAS_DERIVATIVE_FUNCTION = OBJECT_FLAG5,
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
    virtual void dynObjectInitializeA(CoreTime time, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    virtual stringVec getOutputNames() const;
    virtual stringVec getInputNames() const;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;

    IOdata getOutputs(const IOdata& inputs,
                      const StateData& stateData,
                      const SolverMode& sMode) const override;
    virtual double getDoutdt(const IOdata& inputs,
                             const StateData& stateData,
                             const SolverMode& sMode,
                             index_t outputNum = 0) const override;
    virtual double getOutput(const IOdata& inputs,
                             const StateData& stateData,
                             const SolverMode& sMode,
                             index_t outputNum = 0) const override;

    virtual double getOutput(index_t outputNum = 0) const override;

    virtual void updateLocalCache([[maybe_unused]] const IOdata& inputs,
                                  [[maybe_unused]] const StateData& stateData,
                                  [[maybe_unused]] const SolverMode& sMode) override;
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
