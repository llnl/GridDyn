/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../fmi_import/fmiInfo.h"
#include "core/propertyBuffer.h"
#include "fmiSupport.h"
#include "griddyn/gridSubModel.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Fmi2ModelExchangeObject;

enum class FmuMode;  // forward declare enumeration

namespace griddyn::fmi {
class OutputEstimator;
/** class defining a subModel interacting with an FMU v2.0 object for model exchange*/
class FmiMESubModel: public GridSubModel {
  public:
    enum fmiSubModelFlags {
        use_output_estimator = object_flag2,
        fixed_output_interval = object_flag3,
        reprobe_flag = object_flag4,
        has_derivative_function = object_flag5,
    };

  protected:
    count_t m_stateSize = 0;  //!< the total state count
    count_t m_jacElements = 0;  //!< the number of Jacobian elements
    count_t m_eventCount = 0;  //!< the number of event indicators
    std::shared_ptr<Fmi2ModelExchangeObject> me;

    std::vector<OutputEstimator*> oEst;  //!< vector of objects used for output estimation
                                         //!< //TODO:: Make this an actual vector of objects
    coreTime localIntegrationTime = 0.01;
    FmuMode prevFmiState = FmuMode::instantiatedMode;
    std::vector<ValueDependencyInfo> stateInformation;
    std::vector<ValueDependencyInfo> outputInformation;
    std::vector<int> inputVarIndices;
    propertyBuffer paramBuffer;

  private:
    count_t lastSeqID = 0;
    std::vector<double> tempState;
    std::vector<double> tempdState;

  public:
    FmiMESubModel(const std::string& newName = "fmisubmodel2_#",
                  std::shared_ptr<Fmi2ModelExchangeObject> fmi = nullptr);

    FmiMESubModel(std::shared_ptr<Fmi2ModelExchangeObject> fmi = nullptr);
    virtual ~FmiMESubModel();
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

  protected:
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;
    virtual stringVec getOutputNames() const;
    virtual stringVec getInputNames() const;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual index_t findIndex(std::string_view field, const solverMode& sMode) const override;
    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

    virtual std::pair<count_t, count_t>
        LocalRootCount(const solverMode& /* sMode */) const override;
    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode) override;
    virtual void rootTest(const IOdata& inputs,
                          const stateData& sD,
                          double roots[],
                          const solverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
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
    virtual index_t getOutputLoc(const solverMode& sMode, index_t outputNum = 0) const override;

    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
    // for saving the state
    virtual void guessState(coreTime time,
                            double state[],
                            double dstate_dt[],
                            const solverMode& sMode) override;

    virtual void getTols(double tols[], const solverMode& sMode) override;

    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix = "") const override;

    virtual bool isLoaded() const;

    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) override;

  protected:
    void makeSettableState();
    void resetState();
    double getPartial(int depIndex, int refIndex, RefMode mode);
    void probeFMU();
    void loadOutputJac(int index = -1);
    // int searchByRef(fmi2_value_reference_t ref);
};

}  // namespace griddyn::fmi

