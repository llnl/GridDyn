/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "gridSecondary.h"
#include <memory>
#include <string>
#include <vector>

namespace utilities {
class OperatingBoundary;
}  // namespace utilities

namespace griddyn {
class scheduler;
class gridSubModel;
/**
@ brief class describing a generator unit
 a generator is a power production unit in GridDyn.  the base generator class implements methods set
forth in the gridSecondary class and inherits from that class it has mechanics for dealing with the
power flow solution modes of a generator with no dynamics
*/
class Generator: public gridSecondary {
  public:
    /** @brief flags for controlling operation of the generator*/
    enum generator_flags {
        variable_generation = object_flag1,  //!< flag indicating that the generator has
                                             //!< uncontrolled variable generation
        reserve_capable =
            object_flag2,  //!< flag indicating the generator can act as spinning reserve
        agc_capable = object_flag3,  //!< flag indicating the generator is capable of agc response
        use_capability_curve =
            object_flag4,  //!< flag indicating that the generator should use a capability curve
        //! rather than a fixed limit
        no_voltage_derate =
            object_flag5,  //!< flag turning off voltage derating for low voltage power flow
        independent_machine_base = object_flag6,  //!< flag indicating that the generator has a
                                                  //!< different machine base than the simulation
        at_limit = object_flag7,  //!< flag indicating the generator is operating at a limit
        indirect_voltage_control_level =
            object_flag8,  //!< flag indicating that the generator should perform
        //! voltage control indirectly in power flow
        internal_frequency_calculation =
            object_flag9,  //!< flag indicating that the generator computes the frequency internally
        isochronous_operation =
            object_flag10,  //!< flag telling the generator to operation is isochronous mode
    };
    /** @brief enum indicating subModel locations in the subObject structure*/

    static std::atomic<count_t> genCount;  //!< generator count
  protected:
    double P = 0.0;  //!< [pu] Electrical generation real power output
    double Q = 0.0;  //!< [pu] Electrical generation reactive power output
    model_parameter Pset = -kBigNum;  //!< [pu] target power set point
    model_parameter dPdt = 0.0;  //!< define the power ramp
    model_parameter dQdt = 0.0;  //!< define the reactive power ramp
    model_parameter Qmax =
        kBigNum;  //!< [pu mbase] max steady state reactive power values for Power flow analysis
    model_parameter Qmin =
        -kBigNum;  //!< [pu mbase] min steady state reactive power values for Power flow analysis
    model_parameter Qbias =
        0.0;  //!<[pu] targeted Q output for generators with remote voltage control
    model_parameter Pmax =
        kBigNum;  //!< [pu mbase]max steady state real power values for the generator
    model_parameter Pmin =
        -kBigNum;  //!< [pu mbase] min steady state real power values for the generator
    model_parameter participation =
        1.0;  //!< [%]a participation factor used in auto allocating load.
    model_parameter vRegFraction =
        1.0;  //!< [%]  fraction of output reactive power to maintain voltage regulation
    model_parameter machineBasePower = 100;  //!< MW the internal base power of the generator;

    scheduler* sched = nullptr;  //!< alias to pSetControl if pSetControl is a scheduler

    model_parameter m_Vtarget = -1;  //!< voltage target for the generator at the control bus
    model_parameter m_Rs = 0.0;  //!< the real part of the generator impedance
    model_parameter m_Xs = 1.0;  //!< generator impedance defined on Mbase;
    gridBus* remoteBus = nullptr;  //!< the bus for remote control
    std::unique_ptr<utilities::OperatingBoundary> bounds;

  public:
    explicit Generator(const std::string& objName = "gen_$");
    ~Generator();
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;  // for saving the state
    virtual void guessState(coreTime time,
                            double state[],
                            double dstate_dt[],
                            const solverMode& sMode) override;  // for initial setting of the state

    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;
    virtual void setFlag(const std::string& flag, bool val = true) override;

    virtual void add(coreObject* obj) override;
    /** @brief additional add function specific to subModels
    @param[in] a submodel to add
    @throw unrecognizedObjectError is object is not valid*/
    virtual void add(gridSubModel* obj);

    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

    virtual void algebraicUpdate(const IOdata& inputs,
                                 const stateData& sD,
                                 double update[],
                                 const solverMode& sMode,
                                 double alpha) override;
    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& sD,
                              const solverMode& sMode) const override;

    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode) override;
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;
    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix) const override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    /** @brief get the current generator set point
    @return the current generator set point*/
    virtual double getPset() const { return Pset; }
    virtual double getRealPower(const IOdata& inputs,
                                const stateData& sD,
                                const solverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const stateData& sD,
                                    const solverMode& sMode) const override;
    virtual double getRealPower() const override;
    virtual double getReactivePower() const override;
    /** @brief function to set the generator capability curve
    @param[in] Ppts  the points on the curve along the real power axis
    @param[in] Qminpts  the minimum reactive power generation corresponding to the Ppts
    @param[in] Qmaxpts  the maximum reactive power generation corresponding to the Ppts
    */
    virtual void setCapabilityCurve(const std::vector<double>& Ppts,
                                    const std::vector<double>& Qminpts,
                                    const std::vector<double>& Qmaxpts);

    virtual IOdata predictOutputs(coreTime predictionTime,
                                  const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) const override;

    virtual double getAdjustableCapacityUp(coreTime time = maxTime) const override;
    virtual double getAdjustableCapacityDown(coreTime time = maxTime) const override;
    /** @brief get the maximum generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @return the max real power*/
    virtual double getPmax(coreTime time = maxTime) const;
    /** @brief get the maximum reactive generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @param[in] Ptest the real power output corresponding to the desired attainable generation
    @return the max reactive power*/
    virtual double getQmax(coreTime time = maxTime, double Ptest = kNullVal) const;
    /** @brief get the minimum real generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @return the max real power*/
    virtual double getPmin(coreTime time = maxTime) const;
    /** @brief get the minimum reactive generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @param[in] Ptest the real power output corresponding to the desired attainable generation
    @return the min reactive power*/
    virtual double getQmin(coreTime time = maxTime, double Ptest = kNullVal) const;
    /** @brief adjust the output generation by the specified amount
    @param[in] adjustment the value of the desired adjustment
    */
    virtual void generationAdjust(double adjustment);
    virtual change_code powerFlowAdjust(const IOdata& inputs,
                                        std::uint32_t flags,
                                        check_level_t level) override;  // only applicable in pFlow
    virtual coreObject* find(const std::string& object) const override;
    /** get the frequency the generator is operating at
    @param[in] sD the current stateData
    @param[in] sMode the solvermode corresponding to the state
    @param[out] freqOffset the location of the frequency state in the sD arrays
    @return the current frequency the generator is operating at
    */
    virtual double
        getFreq(const stateData& sD, const solverMode& sMode, index_t* freqOffset = nullptr) const;
    /** get the internal angle of the generator
    @param[in] sD the current stateData
    @param[in] sMode the solvermode corresponding to the state
    @param[out] angleOffset the location of the frequency state in the sD arrays
    @return the current angle of  the generator is operating at
    */
    virtual double getAngle(const stateData& sD,
                            const solverMode& sMode,
                            index_t* angleOffset = nullptr) const;

  protected:
    /** set the non-local bus that the generator controls
    @param[in] newRemoteBus the bus the generate is monitoring and controlling*/
    void setRemoteBus(coreObject* newRemoteBus);
};

}  // namespace griddyn
