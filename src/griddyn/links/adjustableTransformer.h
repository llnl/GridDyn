/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "acLine.h"
#include <queue>
#include <string>
#include <vector>

namespace griddyn::links {
/** @brief extends the link class to include adjustments to the tap and tapAngle
*  principally a model for adjustable transformers such as ULTC or regulators or other types of
transformers with adjustments. it implements three types of control in power flow voltage control of
local or remote buses,  local reactive power flow, and local real power flow the real power flow is
the only one that adjusts the angle.  available control include stepped and continuous

*/
class adjustableTransformer: public acLine {
  public:
    /** @brief  enumeration of the available control types
     */
    enum class ControlMode {
        MANUAL_CONTROL = 0,  //!< no automatic adjustments
        VOLTAGE_CONTROL = 1,  //!< automatic control based on voltage
        MW_CONTROL = 2,  //!< automatic control based on real power flow at a specific terminal
        MVAR_CONTROL = 3  //!< automatic control based on real power flow at a specific terminal
    };
    /** @brief  flags for
     */
    enum adjustable_flags {
        continuous_flag = object_flag5,  //!< flag indicating continuous adjustments
        use_target_mode = object_flag6,  //!< flag indicating target mode
        at_limit = object_flag7,  //!< flag indicating the adjustments are at their limit
        no_pFlow_adjustments = object_flag8,  //!< flag turning off all automatic adjustments
        use_lookup_table = object_flag9,  //!< flag indicating use of an impedance lookup table
    };

  protected:
    ControlMode cMode = ControlMode::MANUAL_CONTROL;  //!< control Mode
    model_parameter stepSize =
        0.01;  //!< step size of the adjustment for non-continuous adjustments
    model_parameter maxTapAngle = kPI / 4.0;  //!< maximum tap angle
    model_parameter minTapAngle = -kPI / 4.0;  //!< minimum tap angle
    model_parameter minTap = 0.9;  //!< minimum tap setting
    model_parameter maxTap = 1.1;  //!< maximum tap setting
    model_parameter Vtarget = -kBigNum;  //!< target voltage
    model_parameter Vmax = kBigNum;  //!< maximum voltage before changing the tap
    model_parameter Vmin = 0.0;  //!< minimum voltage before changing the tap

    model_parameter Ptarget = -kBigNum;  //!< the target power flow
    model_parameter Pmin = -kBigNum;  //!< the minimum power level before changing the tap
    model_parameter Pmax = kBigNum;  //!< the maximum power before changing the tap

    model_parameter Qtarget = -kBigNum;  //!< the target reactive power flow
    model_parameter Qmax = kBigNum;  //!< the minimum power level before changing the tap
    model_parameter Qmin = -kBigNum;  //!< the maximum power before changing the tap
    // double Tm;                //!< time constant
    // double Td;                //!< time constant
    double direction = 1;  //!< variable storing whether the directional derivate of the tap
                           //!< changes with respect
    //!< to voltage or power is positive or negative
    model_parameter tapMaxChangeRate = kBigNum;  //!< maximum rate at which the tap can change
    model_parameter sample_rate = 4.0;  //!< [s]the rate at which the measurements are sampled
    gridBus* controlBus = nullptr;  //!< the control bus to monitor voltage

    double tap0 = 0.0;  //!< baseline tap position used for continuous tap settings
    double tapAngle0 = 0.0;  //!< baseline tapAngle position used for continuous tap settings
    model_parameter stepDelay = 30;  //!< step control for adjusting the quantity or the time
                                     //!< constant for continuous system
    model_parameter mp_Tm = 0.05;  //!< time constant for continuous tap settings
    model_parameter dTapdt = 0;  //!< rate of change of the tap
    model_parameter dTapAdt = 0;  //!< rate of change of the tapAngle
  private:
    int controlNum = -1;  //!< the control bus and number setting are not fully determined until
                          //!< initialization so
    //!< this stores information from the startup phase
    std::string controlName;  //!< the control bus and number setting are not fully determined
                              //!< until initialization
    //!< so this stores information from the startup phase
    count_t adjCount = 0;
    count_t oCount = 0;
    double prevAdjust = 0.0;
    double prevValue = 0.0;
    // double baseValue;
  public:
    explicit adjustableTransformer(const std::string& objName = "adjTX_$");
    /** @brief default constructor
@param[in] resistance  resistance of the link
@param[in] reactance  reactance of the link forwarded to the Link constructor
*/
    adjustableTransformer(double resistance,
                          double reactance,
                          const std::string& objName = "adjTX_$");
    //!< @brief destructor
    CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    // adjuster specific functions
    /**@ brief set the control bus to a specified bus pointer
@param[in] cBus  the specified control Bus*/
    void setControlBus(gridBus* cBus);
    /**@ brief set the control bus to a specified bus number
@param[in] busNumber this can be 1 or 2 for already attached buses or the user id of a bus in
which cases the parent of the link is searched for the bus*/
    void setControlBus(index_t busNumber = 2);

    change_code
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level) override;
    void reset(reset_levels level) override;

    void updateLocalCache() override;
    void updateLocalCache(const IOdata& inputs,
                          const stateData& stateData,
                          const solverMode& sMode) override;

    void jacobianElements(const IOdata& inputs,
                          const stateData& stateData,
                          matrixData<double>& matrixDataRef,
                          const IOlocs& inputLocs,
                          const solverMode& sMode) override;
    // for computing all the Jacobian elements at once

    using acLine::ioPartialDerivatives;
    virtual void ioPartialDerivatives(id_type_t busId,
                                      const stateData& stateData,
                                      matrixData<double>& matrixDataRef,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const stateData& stateData,
                                          matrixData<double>& matrixDataRef,
                                          const solverMode& sMode) override;
    virtual void outputPartialDerivatives(id_type_t busId,
                                          const stateData& stateData,
                                          matrixData<double>& matrixDataRef,
                                          const solverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;

    void residual(const IOdata& inputs,
                  const stateData& stateData,
                  double resid[],
                  const solverMode& sMode) override;
    void setState(coreTime time,
                  const double state[],
                  const double dstate_dt[],
                  const solverMode& sMode) override;
    void guessState(coreTime time,
                    double state[],
                    double dstate_dt[],
                    const solverMode& sMode) override;
    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;
    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

  protected:
    void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

  public:
    void rootTest(const IOdata& inputs,
                  const stateData& stateData,
                  double roots[],
                  const solverMode& sMode) override;
    void rootTrigger(coreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const solverMode& sMode) override;
    virtual void followNetwork(int network, std::queue<gridBus*>& stk) override;
    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix = "") const override;

  protected:
    /** @brief compute the Jacobian elements based on the MW control
@param[in] stateData  the stateData of the current state of the system
@param[out] matrixDataRef the matrixData object to store the Jacobian information
@param[in] sMode the solverMode corresponding to the stateData
*/
    void MWJac(const stateData& stateData,
               matrixData<double>& matrixDataRef,
               const solverMode& sMode);
    /** @brief compute the Jacobian elements based on the MVar control
@param[in] stateData  the stateData of the current state of the system
@param[out] matrixDataRef the matrixData object to store the Jacobian information
@param[in] sMode the solverMode corresponding to the stateData
*/
    void MVarJac(const stateData& stateData,
                 matrixData<double>& matrixDataRef,
                 const solverMode& sMode);
    /** @brief compute the partial derivatives of the power flows based on the tap angle
@param[in] busId the id of the calling bus either 1 or 2 or a busID of one of the attached buses
@param[in] stateData  the stateData of the current state of the system
@param[out] matrixDataRef the matrixData object to store the Jacobian information
@param[in] sMode the solverMode corresponding to the stateData
*/
    void tapAnglePartial(index_t busId,
                         const stateData& stateData,
                         matrixData<double>& matrixDataRef,
                         const solverMode& sMode);
    /** @brief compute the partial derivatives of the power flows based on the tap setting
@param[in] busId the id of the calling bus either 1 or 2 or a busID of one of the attached buses
@param[in] stateData  the stateData of the current state of the system
@param[out] matrixDataRef the matrixData object to store the Jacobian information
@param[in] sMode the solverMode corresponding to the stateData
*/
    void tapPartial(index_t busId,
                    const stateData& stateData,
                    matrixData<double>& matrixDataRef,
                    const solverMode& sMode);
    /** @brief do any stepped adjustments  based on voltage control from the power flow
calculations
@return change_code::no_change if nothing was done,  PARAMETER_ADJUSTMENT if the tap changer was
stepped
*/
    change_code voltageControlAdjust();
    /** @brief do any stepped adjustments  based on MW control from the power flow calculations
@return change_code::no_change if nothing was done,  PARAMETER_ADJUSTMENT if the tap changer was
stepped
*/
    change_code MWControlAdjust();
    /** @brief do any stepped adjustments  based on MVAR control from the power flow
calculations
@return change_code::no_change if nothing was done,  PARAMETER_ADJUSTMENT if the tap changer was
stepped
*/
    change_code MVarControlAdjust();

    /** compute a valid tap ratio*/
    double getValidTapRatio(double testTapValue) const;
};

}  // namespace griddyn::links

