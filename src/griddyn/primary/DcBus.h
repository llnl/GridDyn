/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
#include "../GridBus.h"
#include "DcBusControls.h"
#include "utilities/MatrixDataTranslate.hpp"
#include <string>

namespace griddyn {
// forward classes

/** @brief Physical DC network node with an algebraic voltage slot.
 * @details A variable-voltage bus solves real-power balance; PV/SLK power-flow types and
 * FIX_VOLTAGE/DYN_SLK dynamic types constrain the voltage to its stored value instead.
 * Only DC_ONLY or DC_CAPABLE links may attach. Voltage uses the GridBus unit conventions.
 * This models a physical DC network, not the angle-based DC approximation of an AC network.
 * @note Controller registration/merging and local convergence recovery are not implemented
 * to the same extent as AcBus. Do not infer AC controller capabilities from inherited methods.
 */
class DcBus: public GridBus {
    friend class DcBusControls;

  public:
    enum BusFlags {
        USE_AUTOGEN = OBJECT_FLAG2,  //!< indicator if the bus is using an autogen
        SLAVE_BUS = OBJECT_FLAG3,  //!< indicator that the bus is a slave Bus
        MASTER_BUS = OBJECT_FLAG4,  //!< indicator that a bus is a master bus
        DIRECTCONNECT = OBJECT_FLAG5,  //!< indicator that a bus is direct connected to another bus
    };

  protected:
    double vTarget = 1.0;  //!< a target voltage
    double participation = 1.0;
    DcBusControls busController;  //!< local DC controller bookkeeping
    BusType prevType = BusType::PQ;  //!< previous type container if the type automatically changes
    DynBusType prevDynType =
        DynBusType::NORMAL;  //!< previous type container if the type automatically changes
    double dVdP = 0.0;  //!< storage for the dVdP terms from all the secondary objects
    MatrixDataTranslate<1> of;

  public:
    explicit DcBus(const std::string& objName = "dcBus_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    // add components
    using GridBus::add;
    /** @brief Borrow a DC-compatible link, ignoring a duplicate attachment.
     * @pre lnk is non-null
     * @throws UnrecognizedObjectException if the link is not DC compatible
     */
    virtual void add(Link* lnk) override;

    // dynInitializeB

    virtual StateSizes localStateSizes(const SolverMode& sMode) const override;

    virtual count_t localJacobianCount(const SolverMode& sMode) const override;

  protected:
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

  public:
    virtual ChangeCode powerFlowAdjust(const IOdata& inputs,
                                       std::uint32_t flags,
                                       CheckLevel level) override;  // only applicable in pFlow
    // virtual  void generationAdjust(double adjustment);
    virtual void pFlowCheck(std::vector<Violation>& violationVector) override;
    // dynInitializeB dynamics
  protected:
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    // parameter set functions
    /** @brief Select pflowtype/type/bustype or dyntype, using the GridBus type names.
     * @details PQ/AFIX (or NORMAL/FIX_ANGLE dynamically) use voltage balance;
     * PV/SLK (or FIX_VOLTAGE/DYN_SLK) use a fixed-voltage constraint.
     * @throws InvalidParameterValue for an unknown bus type
     */
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void getParameterStrings(stringVec& pstr,
                                     ParamStringType pstype = ParamStringType::all) const override;

    using GridBus::updateLocalCache;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  const SolverMode& sMode) override;

    virtual void guessState(CoreTime time,
                            double state[],
                            double dstateDt[],
                            const SolverMode& sMode) override;
    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;

    /** @brief Cache the derivative of active real-power balance with respect to voltage.
     * @details Intended for Jacobian assembly after updating the bus output cache.
     */
    void computeDerivatives(const StateData& stateDataValue, const SolverMode& sMode);

    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    /** @brief Compatibility recovery hook; currently a no-op for DC buses.
     * @details Use the global network solver for convergence.
     */
    virtual void converge(CoreTime time,
                          double state[],
                          double dstateDt[],
                          const SolverMode& sMode,
                          ConvergeMode mode = ConvergeMode::LOCAL_ITERATION,
                          double tol = 0.01) override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    /** @brief Read voltage [pu] from the mode's voltage slot, or the stored fixed voltage.
     * @details A null state pointer returns the stored value.
     */
    virtual double getVoltage(const double state[], const SolverMode& sMode) const override;
    using GridBus::getVoltage;

    /** @brief Read voltage [pu], including paired algebraic data in a partitioned solve.
     * @details Missing state data falls back to the stored value.
     */
    virtual double getVoltage(const StateData& stateDataValue,
                              const SolverMode& sMode) const override;

    virtual IOlocs getOutputLocs(const SolverMode& sMode) const override;

    virtual index_t getOutputLoc(const SolverMode& sMode, index_t num = 0) const override;

    /** @brief Whether a connected bus uses real-power balance for its voltage equation.
     * @details A voltage slot can still exist when this is false, with a fixed-voltage equation.
     */
    virtual bool useVoltage(const SolverMode& sMode) const;
    /** @brief Return fixed-variable bits; angle is always fixed and voltage follows the bus type.
     */
    virtual int getMode(const SolverMode& sMode) const;
    virtual int propogatePower(bool makeSlack = false) override;

    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix) const override;

  protected:
    void computePowerAdjustments();
};

}  // namespace griddyn
