/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
#include "../gridBus.h"
#include "BusControls.h"
#include "core/coreOwningPtr.hpp"
#include "utilities/matrixDataCompact.hpp"
#include "utilities/matrixDataTranslate.hpp"
#include <string>

namespace griddyn {
class Block;

/** @brief basic power system bus for a power grid simulation
  The gridBus class provides the basic node in a power systems analysis.  It is a locational basis
for voltages and angles.  For the power systems analysis the equations include a power balance
equations \f[ f(V)=\sum Q_{load}+ \sum Q_{gen}+ \sum Q_{link} \f] \f[ f(\theta)=\sum P_{load}+\sum
P_{gen}+\sum P_{link} \f] Buses can have a number of different configurations SLK buses fix the
voltage and angle,  PV buses fix the voltage and real power,  PQ buses have known real and reactive
generation/load, afix buses fix the angle and reactive power.  there are different setting for power
flow calculations and dynamic calculations.

Buses act as connection points for links to tie to other buses and gridSecondary components such as
generators and loads.

*/
class acBus: public gridBus {
    friend class BusControls;

  public:
    /** @brief flags for the buses*/
    enum bus_flags {
        use_autogen = object_flag2,  //!< indicator if the bus is using an autogen
        slave_bus = object_flag3,  //!< indicator that the bus is a slave Bus
        master_bus = object_flag4,  //!< indicator that a bus is a master bus
        directconnect = object_flag5,  //!< indicator that a bus is direct connected to another bus
        identical_PQ_control_objects =
            object_flag6,  //!< indicator that the P and Q control are the same units
        compute_frequency =
            object_flag7,  //!< indicator that the bus should compute the frequency value
        ignore_angle =
            object_flag8,  //!< indicator that the bus should ignore the angle in update functions
        prev_low_voltage_alert =
            object_flag9,  //!< indicator that the bus has triggered a low voltage alert
    };

  protected:
    count_t oCount = 0;  //!< counter for updates
    busType prevType = busType::PQ;  //!< previous type container if the type automatically changes
    dynBusType prevDynType =
        dynBusType::normal;  //!< previous type container if the type automatically changes
    matrixDataCompact<2, 3> partDeriv;  //!< structure containing the partial derivatives
    model_parameter aTarget = 0.0;  //!< an angle Target(for SLK and afix bus types)
    model_parameter vTarget = 1.0;  //!< a target voltage
    model_parameter participation =
        1.0;  //!< overall participation factor in power regulation for an area
    model_parameter refAngle = 0.0;  //!< reference Angle
    model_parameter Vmin = 0;  //!< [pu]    voltage minimum
    model_parameter Vmax = kBigNum;  //!< [pu]    voltage maximum
    model_parameter tieError = 0.0;  //!< tieLine error
    model_parameter prevPower = 0.0;  //!< previous power level
    model_parameter Tw = 0.1;  //!< time constant for the frequency estimator

    coreTime lastSetTime = negTime;  //!< last set time
    coreOwningPtr<Block> fblock;  //!< pointer to frequency estimator block

    BusControls busController;  //!< pointer to the eControls object
    // extra blocks and object for remote controlled buses and bus merging
    matrixDataTranslate<4> of;
    index_t lastSmode = kInvalidLocation;

  public:
    /** @brief default constructor*/
    explicit acBus(const std::string& objName = "bus_$");
    /** @brief alternate constructor to specify voltage and angle
    @param[in] vStart the initial voltage
    @param[in] angleStart the initial angle
    */
    acBus(double vStart, double angleStart, const std::string& objName = "bus_$");

    virtual ~acBus();

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    // add components
    using gridBus::add;
    virtual void add(CoreObject* obj) override;
    /** @brief  add a gridBus object for merging buses*/
    virtual void add(acBus* bus);
    // remove components
    using gridBus::remove;
    virtual void remove(CoreObject* obj) override;

    virtual void remove(acBus* bus);
    // deal with control alerts
    virtual void alert(CoreObject* obj, int code) override;

    // dynInitializeB
    virtual void setOffsets(const solverOffsets& newOffsets, const solverMode& sMode) override;
    virtual void setOffset(index_t offset, const solverMode& sMode) override;

    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

    virtual void setRootOffset(index_t Roffset, const solverMode& sMode) override;

  protected:
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

  public:
    virtual change_code powerFlowAdjust(const IOdata& inputs,
                                        std::uint32_t flags,
                                        check_level_t level) override;  // only applicable in pFlow
    /** @brief  adjust the power levels of the contained adjustable secondary objects
    @param[in] adjustment the amount of the adjustment requested*/
    virtual void generationAdjust(double adjustment) override;
    virtual void pFlowCheck(std::vector<Violation>& violations) override;
    virtual void reset(reset_levels level = reset_levels::minimal) override;
    // dynInitializeB dynamics
  protected:
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void disable() override;
    using gridBus::reconnect;
    virtual void reconnect(gridBus* mapBus) override;
    // parameter set functions
    virtual void getParameterStrings(stringVec& pstr,
                                     paramStringType pstype = paramStringType::all) const override;
    virtual void setFlag(std::string_view flag, bool val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    // solver functions
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode) override;
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const stateData& sD,
                                 double update[],
                                 const solverMode& sMode,
                                 double alpha) override;
    virtual void voltageUpdate(const stateData& sD,
                               double update[],
                               const solverMode& sMode,
                               double alpha) override;
    virtual void guessState(coreTime time,
                            double state[],
                            double dstate_dt[],
                            const solverMode& sMode) override;

    /** @brief  try to shift the states to something more consistent
      called when the current states do not make a consistent condition,  calling converge will
    attempt to move them to a more valid state mode controls how this is done  0- does a single
    iteration loop mode=1 tries to iterate until convergence based on tol mode=2  tries harder
    mode=3 does it with voltage only
    @param[in] time  the time of the corresponding states
    @param[in,out]  state the states of the system at present and shifted to match the updates
    @param[in,out] dstate_dt  the derivatives of the state that get updated
    @param[in] sMode the solvemode matching the states
    @param[in] mode  the mode of the convergence
    @param[in] tol  the convergence tolerance
    */
    virtual void converge(coreTime time,
                          double state[],
                          double dstate_dt[],
                          const solverMode& sMode,
                          converge_mode mode = converge_mode::high_error_only,
                          double tol = 0.01) override;
    /** @brief  try to shift the local states to something more valid
      called when the current states do not make a consistent condition,  calling converge will
    attempt to move them to a more valid state mode controls how this is done  0- does a single
    iteration loop mode=1 tries to iterate until convergence based on tol mode=2  tries harder
    mode=3 does it with voltage only
    @param[in] sMode the solver mode matching the states
    @param[in] mode  the mode of the convergence
    @param[in] tol  the tolerance to converge to
    */
    virtual void localConverge(const solverMode& sMode, int mode = 0, double tol = 0.01);
    /** @brief  return the last error in the real power*/

    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) override;

  protected:
    /** @brief  compute adjustments required for the dynamic update*/
    virtual void computePowerAdjustments();
    /** @brief  compute the partial derivatives based on the given state data
    @param[in] sD  the state Data in question
    @param[in] sMode the solver mode*/
    virtual void computeDerivatives(const stateData& sD, const solverMode& sMode);

  public:
    void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
    /** @brief a faster function to set the voltage and angle of a bus*
    @param[in] Vnew  the new voltage
    @param[in] Anew  the new angle
    */
    virtual void setVoltageAngle(double Vnew, double Anew) override;
    // for identifying which variables are algebraic vs differential
    virtual void getVariableType(double sdata[],
                                 const solverMode& sMode) override;  // only applicable in DAE
    // mode
    virtual void getTols(double tols[], const solverMode& sMode) override;
    // dynamic simulation
    virtual stringVec localStateNames() const override;

    /** @brief find a link based on the bus desiring to be connected to
    @param[in] makeSlack  flag indicating that the bus should be made a slack bus after propagating
    the power
    @return  a pointer to a Link that connects the current bus to the bus specified by bs or nullptr
    if none exists
    */
    virtual int propogatePower(bool makeSlack = false) override;

    /** @brief get the maximum real power generation
     * @return the maximum real power generation
     **/
    virtual double getMaxGenReal() const override;
    /** @brief get the maximum reactive power generation
     * @return the maximum reactive power generation
     **/
    virtual double getMaxGenReactive() const override;

    /** @brief get the available controllable upward adjustments within a time period
    @ details this means power production or load reduction
    @param[in] time  the time period within which to do the adjustments
    * @return the reactive link power
    **/
    virtual double getAdjustableCapacityUp(coreTime time = maxTime) const override;
    /** @brief get the available controllable upward adjustments within a time period
    @ details this means power production or load reduction
    @param[in] time  the time period within which to do the adjustments
    * @return the reactive link power
    **/
    virtual double getAdjustableCapacityDown(coreTime time = maxTime) const override;
    /** @brief the dPdf partial derivative  (may be deprecated in the future)
     * @return the $\frac{\partial P}{\partial f}$
     **/
    virtual double getdPdf() const override;

    /** @brief get the tie error (may be deprecated in the future)
     * @return the tie error
     **/
    virtual double getTieError() const override;

    /** @brief get the frequency response
     * @return the tie error
     **/
    virtual double getFreqResp() const override;

    /** @brief get available regulation
     * @return the available regulation
     **/
    virtual double getRegTotal() const override;

    /** @brief get the scheduled power
     * @return the scheduled power
     **/
    virtual double getSched() const override;

    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& sD,
                              const solverMode& sMode) const override;
    virtual index_t getOutputLoc(const solverMode& sMode, index_t num) const override;

    virtual IOlocs getOutputLocs(const solverMode& sMode) const override;
    /** @brief get the voltage
    * @param[in] state the system state
    @param[in] sMode the corresponding solverMode to the state
    @return the bus voltage
    **/
    virtual double getVoltage(const double state[], const solverMode& sMode) const override;
    /** @brief get the angle
    * @param[in] state the system state
    @param[in] sMode the corresponding solverMode to the state
    @return the bus angle
    **/
    virtual double getAngle(const double state[], const solverMode& sMode) const override;
    /** @brief get the voltage
    * @param[in] sD the system state data
    @param[in] sMode the corresponding solverMode to the state data
    @return the bus voltage
    **/
    virtual double getVoltage(const stateData& sD, const solverMode& sMode) const override;
    /** @brief get the angle
    * @param[in] sD the system state data
    @param[in] sMode the corresponding solverMode to the state
    @return the bus angle
    **/
    virtual double getAngle(const stateData& sD, const solverMode& sMode) const override;
    /** @brief get the bus frequency
    * @param[in] sD the system state data
    @param[in] sMode the corresponding solverMode to the state
    @return the bus frequency
    **/
    virtual double getFreq(const stateData& sD, const solverMode& sMode) const override;

    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode,
                                  check_level_t level) override;
    /** @brief function used for returning the mode of the bus
     depends on the interaction of the SolverInterface and the bus type
    @param[in] sMode the corresponding solverMode to the state
    @return the system mode
    **/
    virtual int getMode(const solverMode& sMode) const;
    /** @brief function to determine there is a state representing the angle
    @param[in] sMode the corresponding solverMode to the state
    @return true if there is an angle state false otherwise
    **/
    virtual bool useAngle(const solverMode& sMode) const;
    /** @brief function to determine there is a state representing the voltage
    @param[in] sMode the corresponding solverMode to the state
    @return true if there is an voltage state false otherwise
    **/
    virtual bool useVoltage(const solverMode& sMode) const;

    virtual void updateFlags(bool dynOnly = false) override;
    // for registering and removing power control objects

    /** @brief  register an object for voltage control on a bus*/
    void registerVoltageControl(GridComponent* comp) override;
    /** @brief  remove an object from voltage control on a bus*/
    void removeVoltageControl(GridComponent* comp) override;
    /** @brief  register an object for power control on a bus*/
    void registerPowerControl(GridComponent* comp) override;
    /** @brief  remove an object from power control on a bus*/
    void removePowerControl(GridComponent* comp) override;

    // for dealing with buses merged with zero impedance link
    /** @brief  merge a bus with the calling bus*/
    virtual void mergeBus(gridBus* mbus) override;
    /** @brief  unmerge a bus with the calling bus*/
    virtual void unmergeBus(gridBus* mbus) override;
    /** @brief  check if all the buses that are merged should be*/
    virtual void checkMerge() override;

  protected:
    /** @brief compute the current power-balance error
    @param[in] sD stateData the stateData from which to compute the error
    @param[in] sMode the solverMode corresponding to the stateData
    @return the error in the power balance equations
    */
    virtual double computeError(const stateData& sD, const solverMode& sMode) override;

  private:
    double getAverageAngle() const;
    count_t getDependencyCount(const solverMode& sMode) const;
    Generator* keyGen = nullptr;
};

}  // namespace griddyn
