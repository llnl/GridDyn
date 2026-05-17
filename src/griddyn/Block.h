/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridSubModel.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
namespace blocks {
    class valueLimiter;
    class rampLimiter;
}  // namespace blocks

/** @brief class implementing basic control system block
 the basic block class takes a single input X  the output is then \f$K*(X+bias)\f$
optionally implementing limiters Omax and Omin  the limiters have a reset level specified by
resetLevel once the object is initialized the determination of whether to use the ramps is fixed and
cannot be changed unless the object is reinitialized directly

the blocks take 1 or 2 inputs the first being the single input,  if the differential input is set
then the second argument is the time derivative of the input
*/
class Block: public GridSubModel {
  public:
    /** @brief flags common for all control blocks
     */
    enum controller_flags {
        step_only = object_flag1,  //!< flag indicating that the block does not have any state
        use_state = object_flag2,  //!< flag indicating that the basic block should not control the
                                   //!< state before the limiters
        has_limits = object_armed_flag,  //!< flag indicating the block has limits of some kind
        use_block_limits = object_flag3,  //!< flag indicating the block has upper and lower limits
        use_ramp_limits = object_flag4,  //!< flag indicating the block has ramp limits

        differential_input =
            object_flag5,  //!< flag indicating that the input is a differential state
        use_direct =
            object_flag6,  //!< flag indicating that the block should just use the input directly
        simplified = object_flag7,  //!< flag indicating that the block should revert to basic block
                                    //!< behavior [used
        //!< only by derived object]
        anti_windup_limits = object_flag8,  //!< flag indicating that the limits should be
                                            //!< anti-windup [used only by derived objects]
    };

  protected:
    model_parameter K = 1.0;  //!<  gain
    model_parameter Omax = kBigNum;  //!< max output value
    model_parameter Omin = -kBigNum;  //!< min output value
    model_parameter rampMax = kBigNum;  //!< rate of change max value
    model_parameter rampMin = -kBigNum;  //!< rate of change min value
    model_parameter bias = 0.0;  //!< bias
    model_parameter resetLevel =
        -0.001;  //!< the level below or above the max/min that the limiters should be removed
    double prevInput = 0.0;  //!< variable to hold previous input values;
    count_t limiter_alg = 0;  //!< the number of algebraic states used by the limiters
    count_t limiter_diff = 0;  //!< the number of differential states used by the limiters
    std::string outputName = "output";  //!< the name of the output state
    std::unique_ptr<blocks::valueLimiter>
        vLimiter;  //!< a pointer to an object that handles the value limits
    std::unique_ptr<blocks::rampLimiter>
        rLimiter;  //!< a pointer to an object that handles the ramp limits
  public:
    /** @brief default constructor*/
    explicit Block(const std::string& objName = "block_#");
    /** @brief alternate constructor
    @param[in] gain  the desired gain of the block
    */
    Block(double gain, const std::string& objName = "block_#");

    virtual ~Block();  // included for separation of types in unique Pointers
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

  protected:
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    /** simplified initialization function for the block
    @details wraps the call to dynInitializeB to simplify things for a block
    @param[in] input  the initial input to the block
    @param[in] desiredOutput the initial desired output for the block
    @return if input is kNullVal it returns the input required to make the output match
    desiredOutput otherwise it returns the initial output
    */
    double blockInitialize(double input, double desiredOutput);

    virtual void setFlag(std::string_view flag, bool val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    // virtual void derivative(const IOdata &inputs, const stateData &sD, double deriv[], const
    // solverMode &sMode);

    /** @brief simplifying function in place of residual since block have only one input/output
    @param[in] input  the block input
    @param[in] didt the input derivative used if the differential input flag is set and it is needed
    otherwise ignored
    @param[in] sD the state data
    @param[out] resid the location to store the Jacobian elements
    @param[in] sMode the solverMode that corresponds to the state data
    */
    virtual void blockResidual(double input,
                               double didt,
                               const stateData& stateDataValue,
                               double resid[],
                               const solverMode& solverModeValue);

    virtual void residual(const IOdata& inputs,
                          const stateData& stateDataValue,
                          double resid[],
                          const solverMode& solverModeValue) override;

    /** @brief simplifying function in place of derivative call since block have only one
    input/output
    @param[in] input  the block input
    @param[in] didt the input derivative used if the differential input flag is set and it is needed
    otherwise ignored
    @param[in] sD the state data
    @param[out] deriv the location to store the derivative elements
    @param[in] sMode the solverMode that corresponds to the state data
    */
    virtual void blockDerivative(double input,
                                 double didt,
                                 const stateData& stateDataValue,
                                 double deriv[],
                                 const solverMode& solverModeValue);
    virtual void derivative(const IOdata& inputs,
                            const stateData& stateDataValue,
                            double deriv[],
                            const solverMode& solverModeValue) override;

    /** @brief simplifying function in place of algebraicUpdate call since block have only one
    input/output
    @param[in] input  the block input
    @param[in] sD the state data
    @param[out] update the location to store the algebraic update elements
    @param[in] sMode the solverMode that corresponds to the state data
    */
    virtual void blockAlgebraicUpdate(double input,
                                      const stateData& stateDataValue,
                                      double update[],
                                      const solverMode& solverModeValue);
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const stateData& stateDataValue,
                                 double update[],
                                 const solverMode& solverModeValue,
                                 double alpha) override;

    /** @brief simplifying function in place of Jacobian elements since block have only one
    input/output
    @param[in] input  the block input
    @param[in] didt the input derivative used if the differential input flag is set and it is needed
    otherwise ignored
    @param[in] sD the state data
    @param[out] md the location to store the Jacobian elements
    @param[in] argLoc the index location of the input
    @param[in] sMode the solverMode that corresponds to the state data
    */
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const stateData& stateDataValue,
                                       matrixData<double>& matrixDataValue,
                                       index_t argLoc,
                                       const solverMode& solverModeValue);

    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  matrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const solverMode& solverModeValue) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    /** @brief simplifying function in place of timestep since block have only one input/output
    @param[in] time  the time to step to
    @param[in] input  the input argument
    @return the output
    */
    virtual double step(coreTime time, double input);
    virtual void rootTest(const IOdata& inputs,
                          const stateData& stateDataValue,
                          double roots[],
                          const solverMode& solverModeValue) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& solverModeValue) override;
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  const solverMode& solverModeValue,
                                  check_level_t level) override;
    // virtual void setTime(coreTime time){prevTime=time;};
    virtual stringVec localStateNames() const override;
    /** get the single output for the block
    @param[in] sD the state data to use in computing the output
    @param[in] sMode the solverMode associated with the stateData
    */
    virtual double getBlockOutput(const stateData& stateDataValue,
                                  const solverMode& solverModeValue) const;
    /** get the single output for the block based on local information
     */
    virtual double getBlockOutput() const;
    /** get the time derivative of the block output -should only be used for block with a
    differential output
    @param[in] sD the state data to use in computing the output
    @param[in] sMode the solverMode associated with the stateData
    */
    virtual double getBlockDoutDt(const stateData& stateDataValue,
                                  const solverMode& solverModeValue) const;
    /**get the time derivative of the block output -should only be used for block with a
     * differential output based on local information
     */
    virtual double getBlockDoutDt() const;
    /** get the name of the output for a block*/
    const std::string& getOutputName() const { return outputName; }

  protected:
    /** compute the elements of the residual associated with the limiter
    @param[in] input the input to the block
    @param[in] didt the time derivative of the input of the block
    @param[in] sD the stateData associated with a block
    @param[in] resid the memory location to store the residual
    @param[in] sMode the solverMode associated with the state Data
    */
    void limiterResidElements(double input,
                              double didt,
                              const stateData& stateDataValue,
                              double resid[],
                              const solverMode& solverModeValue);
    /** get the input that goes into the rate limiter*/
    static double getRateInput(const IOdata& inputs);

  private:
    /** get the value to test on a value limiter*/
    double getTestValue(double input, double currentState) const;
    /** get the value to test on the rate limiter*/
    double getTestRate(double didt, double currentStateRate) const;
    /** check if the object uses a value state */
    bool hasValueState() const;
    /** update the internal information associated with the value limiter*/
    void valLimiterUpdate();
    /** update the internal information associated with the ramp limiter*/
    void rampLimiterUpdate();
    /** generate a default reset level for the limiters*/
    double computeDefaultResetLevel() const;
    /** generate the value to test based incoming information for the limiter*/
    double getLimiterTestValue(double input,
                               const stateData& stateDataValue,
                               const solverMode& solverModeValue);
};

/** @brief generate a shared pointer to a block based on a string input
@param[in] blockstr  a string defining a block
@return a unique pointer to a block
*/
std::unique_ptr<Block> make_block(const std::string& blockstr);

}  // namespace griddyn
