/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GridSecondary.h"
#include <memory>
#include <string>
#include <vector>

namespace utilities {
class OperatingBoundary;
}  // namespace utilities

namespace griddyn {
class Scheduler;
class GridSubModel;
/**
@ brief class describing a generator unit
 a generator is a power production unit in GridDyn.  the base generator class implements methods set
forth in the GridSecondary class and inherits from that class it has mechanics for dealing with the
power flow solution modes of a generator with no dynamics
*/
class Generator: public GridSecondary {
  public:
    /** @brief flags for controlling operation of the generator*/
    enum GeneratorFlags {
        VARIABLE_GENERATION = OBJECT_FLAG1,  //!< flag indicating that the generator has
                                             //!< uncontrolled variable generation
        RESERVE_CAPABLE =
            OBJECT_FLAG2,  //!< flag indicating the generator can act as spinning reserve
        AGC_CAPABLE = OBJECT_FLAG3,  //!< flag indicating the generator is capable of agc response
        USE_CAPABILITY_CURVE =
            OBJECT_FLAG4,  //!< flag indicating that the generator should use a capability curve
        //! rather than a fixed limit
        NO_VOLTAGE_DERATE =
            OBJECT_FLAG5,  //!< flag turning off voltage derating for low voltage power flow
        INDEPENDENT_MACHINE_BASE = OBJECT_FLAG6,  //!< flag indicating that the generator has a
                                                  //!< different machine base than the simulation
        AT_LIMIT = OBJECT_FLAG7,  //!< flag indicating the generator is operating at a limit
        INDIRECT_VOLTAGE_CONTROL_LEVEL =
            OBJECT_FLAG8,  //!< flag indicating that the generator should perform
        //! voltage control indirectly in power flow
        INTERNAL_FREQUENCY_CALCULATION =
            OBJECT_FLAG9,  //!< flag indicating that the generator computes the frequency internally
        ISOCHRONOUS_OPERATION =
            OBJECT_FLAG10,  //!< flag telling the generator to operation is isochronous mode
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

    Scheduler* sched = nullptr;  //!< alias to pSetControl if pSetControl is a scheduler

    model_parameter m_Vtarget = -1;  //!< voltage target for the generator at the control bus
    model_parameter m_Rs = 0.0;  //!< the real part of the generator impedance
    model_parameter m_Xs = 1.0;  //!< generator impedance defined on Mbase;
    GridBus* remoteBus = nullptr;  //!< the bus for remote control
    std::unique_ptr<utilities::OperatingBoundary> bounds;

  public:
    explicit Generator(const std::string& objName = "gen_$");
    ~Generator();
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const SolverMode& sMode) override;  // for saving the state
    virtual void guessState(CoreTime time,
                            double state[],
                            double dstate_dt[],
                            const SolverMode& sMode) override;  // for initial setting of the state

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;

    virtual void add(CoreObject* obj) override;
    /** @brief additional add function specific to subModels
    @param[in] obj submodel to add
    @throw unrecognizedObjectError is object is not valid*/
    virtual void add(GridSubModel* obj);

    virtual StateSizes localStateSizes(const SolverMode& sMode) const override;

    virtual count_t localJacobianCount(const SolverMode& sMode) const override;

    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual IOdata getOutputs(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode) const override;

    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          MatrixData<double>& matrixDataValue,
                                          const SolverMode& sMode) override;
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      MatrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix) const override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    /** @brief get the current generator set point
    @return the current generator set point*/
    virtual double getPset() const { return Pset; }
    virtual double getRealPower(const IOdata& inputs,
                                const StateData& stateDataValue,
                                const SolverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const StateData& stateDataValue,
                                    const SolverMode& sMode) const override;
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

    virtual IOdata predictOutputs(CoreTime predictionTime,
                                  const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  const SolverMode& sMode) const override;

    virtual double getAdjustableCapacityUp(CoreTime time = maxTime) const override;
    virtual double getAdjustableCapacityDown(CoreTime time = maxTime) const override;
    /** @brief get the maximum generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @return the max real power*/
    virtual double getPmax(CoreTime time = maxTime) const;
    /** @brief get the maximum reactive generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @param[in] Ptest the real power output corresponding to the desired attainable generation
    @return the max reactive power*/
    virtual double getQmax(CoreTime time = maxTime, double Ptest = kNullVal) const;
    /** @brief get the minimum real generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @return the max real power*/
    virtual double getPmin(CoreTime time = maxTime) const;
    /** @brief get the minimum reactive generation attainable in a specific amount of time
    @param[in] time  the time window to achieve the generation
    @param[in] Ptest the real power output corresponding to the desired attainable generation
    @return the min reactive power*/
    virtual double getQmin(CoreTime time = maxTime, double Ptest = kNullVal) const;
    /** @brief adjust the output generation by the specified amount
    @param[in] adjustment the value of the desired adjustment
    */
    virtual void generationAdjust(double adjustment);
    virtual ChangeCode powerFlowAdjust(const IOdata& inputs,
                                       std::uint32_t flags,
                                       CheckLevel level) override;  // only applicable in pFlow
    virtual CoreObject* find(std::string_view object) const override;
    /** get the frequency the generator is operating at
    @param[in] stateDataValue the current stateData
    @param[in] sMode the SolverMode corresponding to the state
    @param[out] freqOffset the location of the frequency state in the sD arrays
    @return the current frequency the generator is operating at
    */
    virtual double getFreq(const StateData& stateDataValue,
                           const SolverMode& sMode,
                           index_t* freqOffset = nullptr) const;
    /** get the internal angle of the generator
    @param[in] stateDataValue the current stateData
    @param[in] sMode the SolverMode corresponding to the state
    @param[out] angleOffset the location of the frequency state in the sD arrays
    @return the current angle of  the generator is operating at
    */
    virtual double getAngle(const StateData& stateDataValue,
                            const SolverMode& sMode,
                            index_t* angleOffset = nullptr) const;

  protected:
    /** set the non-local bus that the generator controls
    @param[in] newRemoteBus the bus the generate is monitoring and controlling*/
    void setRemoteBus(CoreObject* newRemoteBus);
};

}  // namespace griddyn
