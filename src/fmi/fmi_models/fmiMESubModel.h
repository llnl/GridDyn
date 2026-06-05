/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../fmi_import/fmiInfo.h"
#include "core/PropertyBuffer.h"
#include "fmiSupport.h"
#include "griddyn/GridSubModel.h"
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
    enum FmiSubModelFlags {
        USE_OUTPUT_ESTIMATOR = OBJECT_FLAG2,
        FIXED_OUTPUT_INTERVAL = OBJECT_FLAG3,
        REPROBE_FLAG = OBJECT_FLAG4,
        HAS_DERIVATIVE_FUNCTION = OBJECT_FLAG5,
    };

  protected:
    count_t m_stateSize = 0;  //!< the total state count
    count_t m_jacElements = 0;  //!< the number of Jacobian elements
    count_t m_eventCount = 0;  //!< the number of event indicators
    std::shared_ptr<Fmi2ModelExchangeObject> me;

    std::vector<OutputEstimator*> oEst;  //!< vector of objects used for output estimation
                                         //!< //TODO:: Make this an actual vector of objects
    CoreTime localIntegrationTime = 0.01;
    FmuMode prevFmiState = FmuMode::INSTANTIATED_MODE;
    std::vector<ValueDependencyInfo> stateInformation;
    std::vector<ValueDependencyInfo> outputInformation;
    std::vector<int> inputVarIndices;
    PropertyBuffer paramBuffer;

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
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    virtual stringVec getOutputNames() const;
    virtual stringVec getInputNames() const;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
    virtual StateSizes localStateSizes(const SolverMode& sMode) const override;

    virtual count_t localJacobianCount(const SolverMode& sMode) const override;

    virtual std::pair<count_t, count_t>
        LocalRootCount(const SolverMode& /* sMode */) const override;
    virtual void residual(const IOdata& inputs,
                          const StateData& stateData,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& stateData,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateData,
                                          MatrixData<double>& matrixData,
                                          const SolverMode& sMode) override;
    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateData,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
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
    virtual index_t getOutputLoc(const SolverMode& sMode, index_t outputNum = 0) const override;

    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;
    // for saving the state
    virtual void guessState(CoreTime time,
                            double state[],
                            double dstateDt[],
                            const SolverMode& sMode) override;

    virtual void getTols(double tols[], const SolverMode& sMode) override;

    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix = "") const override;

    virtual bool isLoaded() const;

    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& stateData,
                                  const SolverMode& sMode) override;

  protected:
    void makeSettableState();
    void resetState();
    double getPartial(int depIndex, int refIndex, RefMode mode);
    void probeFMU();
    void loadOutputJac(int index = -1);
    // int searchByRef(fmi2_value_reference_t ref);
};

}  // namespace griddyn::fmi
