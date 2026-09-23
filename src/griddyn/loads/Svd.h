/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RampLoad.h"
#include <cstdint>
#include <string>
#include <vector>

namespace griddyn::loads {
/** @brief defining the interface for a static var device*/
class Svd: public RampLoad {
  public:
    /** flags used for Svd operation*/
    enum SvdFlags {
        CONTINUOUS_FLAG = OBJECT_FLAG6,
        LOCKED_FLAG = OBJECT_FLAG7,
        REACTIVE_CONTROL_FLAG = OBJECT_FLAG8,
        REVERSE_CONTROL_FLAG = OBJECT_FLAG9,
        REVERSE_TOGGLED_FLAG = OBJECT_FLAG10,  // indicator that the reverse flag has been
                                               // toggled so don't try it again
        AT_LIMIT_FLAG = OBJECT_FLAG11,
    };

  protected:
    GridBus* controlBus = nullptr;  //!< pointer to the control bus
    model_parameter Qmin = -kBigNum;  //!<[puMVA] the minimum reactive power
    model_parameter Qmax = kBigNum;  //!<[puMVA] the maximum reactive power output
    model_parameter Vmin = 0.8;  //!<[puV] the low voltage threshold
    model_parameter Vmax = 1.2;  //!<[puV] the high voltage threshold

    model_parameter Qlow = 0.0;  //!<[puMVA] the lowest available Q block level
    model_parameter Qhigh = kBigNum;  //!<[puMVA] the maximum reactive power block level
    int currentStep = 0;  //!< the current step level
    int stepCount = 0;  //!< the total number of steps available
    std::vector<std::pair<int, double>>
        Cblocks;  // a vector containing the capacitive blocks (count, size[puMW])

    int adjustmentMethod = 0;  //!< RAW ADJM metadata; exact PSS/E switching method is not modeled

    model_parameter participation = 1.0;  //!< a participation factor

    // Optional ANDES switched-shunt representation. Values are stored on the
    // GridDyn system base. Ordinary Svd/PSS/E behavior is unchanged when this
    // bank is not configured.
    bool andesBankMode = false;
    std::vector<double> andesGs;
    std::vector<double> andesBs;
    std::vector<int> andesNs;
    model_parameter andesVref = 1.0;
    model_parameter andesDv = 0.05;
    model_parameter andesDt = 30.0;
    model_parameter andesBaseG = 0.0;
    model_parameter andesBaseB = 0.0;
    int andesStep = 0;
    CoreTime andesLastSwitchTime = negTime;
    int minIter = 2;  //!< minimum power-flow iteration before switching is enabled
    model_parameter errTol = 0.01;  //!< power-flow error threshold that enables switching

  public:
    Svd(const std::string& objName = "svd_$");
    Svd(double realPower, double reactivePower, const std::string& objName = "svd_$");
    virtual ~Svd();

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void setLoad(double level, units::unit unitType = units::defunit) override;
    virtual void
        setLoad(double plevel, double qlevel, units::unit unitType = units::defunit) override;
    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;  // for saving the state
    virtual void guessState(CoreTime time,
                            double state[],
                            double dstate_dt[],
                            const SolverMode& sMode) override;  // for initial setting of the state

    virtual void getVariableType(double sdata[], const SolverMode& sMode) override;

    virtual StateSizes localStateSizes(const SolverMode& sMode) const override;
    virtual count_t localJacobianCount(const SolverMode& sMode) const override;

    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& stateData,
                                  const SolverMode& sMode) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    /** define which bus the Svd is controlling voltage on if it is not otherwise specified it
     * is assumed to be the parent bus
     */
    virtual void setControlBus(GridBus* cBus);

    /** add a reactive block to the controller
@param[in] steps the number of steps in the block
@param[in] qstep  the size of each step
@param[in] unitType  the units of qstep
*/
    void addBlock(int steps, double qstep, units::unit unitType = units::defunit);

    /** Set the presently selected stepped level without changing the supplied output.
     *
     * RAW BINIT is the actual initial susceptance, while the block list defines
     * the levels available to subsequent control actions.  The reader uses this
     * method after adding all blocks so a first adjustment starts from BINIT.
     */
    void setInitialReactivePower(double level, units::unit unitType = units::defunit);

    /** Configure an ANDES ShuntSw block bank using system-base admittances. */
    void configureAndesShunt(const std::vector<double>& conductanceSteps,
                             const std::vector<double>& susceptanceSteps,
                             const std::vector<int>& stepCounts,
                             double vref,
                             double voltageDelta,
                             double timeDelay,
                             double initialG,
                             double initialB);

    virtual ChangeCode
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, CheckLevel level) override;
    virtual void reset(ResetLevels level = ResetLevels::MINIMAL) override;

    virtual void residual(const IOdata& inputs,
                          const StateData& stateData,
                          double resid[],
                          const SolverMode& sMode) override;

    virtual void derivative(const IOdata& inputs,
                            const StateData& sD,
                            double deriv[],
                            const SolverMode& sMode) override;

    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateData,
                                          MatrixData<double>& matrixData,
                                          const SolverMode& sMode) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix) const override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const StateData& sD,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& sD,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

  protected:
    double levelForStep(int step) const;
    int nearestStep(double level) const;
    int voltageControlStep(double voltage) const;
    bool powerFlowAdjustmentAllowed(const IOdata& inputs) const;

    int andesMaxStep() const;
    int andesInitialStep() const;
    double andesEffectiveValue(const std::vector<double>& blocks, double baseValue, int step) const;
    void updateAndesAdmittance();
    bool adjustAndesStep(int direction);

    /** get the setting corresponding to a specific output level
@param[in] level the reactive output level desired [puMW]
@return the step number corresponding to that level (best effort)
*/
    virtual int checkSetting(double level);
    /** change the output setting to correspond to a specific step number
@param step the step number for the update
*/
    virtual void updateSetting(int step);
};
}  // namespace griddyn::loads
