/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_SUBSYSTEM_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_SUBSYSTEM_H_

#include "../Area.h"
#include "../Link.h"
#include <queue>
#include <string>
#include <vector>

namespace griddyn {
/** @brief class defining a subsystem which is a set of components which link other components
 built on the link model a subsystem contains an area so the whole simulation can be contained in
 layers
*/
class subsystem: public Link {
  public:
    enum subsystem_flags {
        direct_connection = object_flag5,  //!< flag indicating directly connected objects (skipping
                                           //!< the terminal link structure)
    };

  protected:
    count_t m_terminals = 0;  //!< the number of terminals
    std::vector<Link*>
        terminalLink;  //!< list of internal links which make the external connections
    std::vector<index_t>
        cterm;  //!< the index of the terminal on the terminalLinks which make the connection
    std::vector<gridBus*>
        terminalBus;  //!< list of buses which attach to the external terminal points

    Area subarea;  //!<  a container area
    std::vector<double> Pout;  //!< vector of output powers on each of the terminals
    std::vector<double> Qout;  //!< vector of output reactive powers on each of the terminals
  public:
    /** @brief default constructor
  @param[in] terminals  the number of terminal the subsystem should have*/
    subsystem(count_t terminals, const std::string& objName = "subsystem_$");

    subsystem(const std::string& objName = "subsystem_$");
    /** @brief the destructor*/
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    // add components
    virtual void add(coreObject* obj) override;
    // remove components
    virtual void remove(coreObject* obj) override;

    // get component models
    virtual gridBus* getBus(index_t num) const override;
    virtual Link* getLink(index_t num) const override;
    virtual Relay* getRelay(index_t num) const override;
    virtual Area* getArea(index_t num) const override;
    // dynInitializeB

    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void pFlowCheck(std::vector<Violation>& Violation_vector) override;
    // dynInitializeB dynamics
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    /** @brief relic of something not used to my knowledge*/
    virtual void updateTheta(coreTime /*time*/) {}

    // parameter set functions
    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    void setAll(const std::string& type,
                const std::string& param,
                double val,
                units::unit unitType = units::defunit) override;

    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    // find components
    virtual coreObject* find(const std::string& objName) const override;
    virtual coreObject* getSubObject(const std::string& typeName, index_t num) const override;
    virtual coreObject* findByUserID(const std::string& typeName, index_t searchID) const override;
    // solver functions

    virtual change_code
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level) override;

    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
    // for identifying which variables are algebraic vs differential
    /** @brief do a local converge on the components in the area
   a wrapper around the area->converge function
  @param[in] time the time
  @param[in] state the system state
  @param[in] dstate_dt the time derivative of the state
  @param[in] sMode the solverMode corresponding to the state
  @param[in] tol  the tolerance to do the convergence
  @param[in] mode the mode of convergence
  */
    virtual void converge(coreTime time,
                          double state[],
                          double dstate_dt[],
                          const solverMode& sMode,
                          converge_mode mode = converge_mode::block_iteration,
                          double tol = 0.01) override;
    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) override;

    virtual void reset(reset_levels level) override;
    // root finding functions

    // grab information

    /** @brief flag all the voltage states
  *@param[out] vStates a vector with a value of 1.0 for all voltage states and 0 otherwise
  @param[in] sMode the solverMode to get the voltage state indicators for
  */
    void getVoltageStates(double vStates[], const solverMode& sMode);

    bool switchTest() const override;
    bool switchTest(index_t num) const override;
    void switchMode(index_t num, bool mode) override;
    // is connected
    virtual bool isConnected() const override;

    virtual int fixRealPower(double power,
                             id_type_t measureTerminal,
                             id_type_t fixedterminal = 0,
                             units::unit unitType = units::defunit) override;
    virtual int fixPower(double rPower,
                         double qPower,
                         id_type_t measureTerminal,
                         id_type_t fixedterminal = 0,
                         units::unit unitType = units::defunit) override;

    virtual void followNetwork(int network, std::queue<gridBus*>& stk) override;
    virtual void updateBus(gridBus* bus, index_t busnumber) override;

    virtual double quickupdateP() override;

    double remainingCapacity() const override;
    double getAngle() const override;
    double getAngle(const double state[], const solverMode& sMode) const override;
    virtual double getRealImpedance(id_type_t busId = invalid_id_value) const override;
    virtual double getImagImpedance(id_type_t busId = invalid_id_value) const override;
    virtual double getTotalImpedance(id_type_t busId = invalid_id_value) const override;
    virtual double getCurrent(id_type_t busId = invalid_id_value) const override;
    virtual double getRealCurrent(id_type_t busId = invalid_id_value) const override;
    virtual double getImagCurrent(id_type_t busId = invalid_id_value) const override;

    virtual double getRealPower(
        id_type_t busId = invalid_id_value) const override;  // function to return the real flow in
    virtual double getReactivePower(id_type_t busId = invalid_id_value)
        const override;  // function to return the reactive power in
    virtual count_t terminalCount() const override { return m_terminals; }
    double getLoss() const override;
    double getReactiveLoss() const override;
    virtual double getMaxTransfer() const override;

    // dynInitializeB power flow

    // for computing all the Jacobian elements at once
    using Link::ioPartialDerivatives;
    virtual void ioPartialDerivatives(id_type_t busId,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
    using Link::outputPartialDerivatives;
    virtual void outputPartialDerivatives(id_type_t busId,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode) override;

    // virtual void busResidual(index_t busId, const stateData &sD, double *Fp, double *Fq, const
    // solverMode &sMode);
    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& sD,
                              const solverMode& sMode) const override;
    virtual IOdata
        getOutputs(id_type_t busId, const stateData& sD, const solverMode& sMode) const override;
    // TODO(phlpt): Add the other getOutput functions.
  protected:
    /** @brief get a vector with pointers to all the buses
   wrapper around the corresponding area function
  @param[out] busVector a vector of all the buses
  @param[in] start the index where to start placing the buses
  */
    count_t getBusVector(std::vector<gridBus*>& busVector, index_t start = 0);
    /** @brief change the number of terminals
  @param[in] count  the desired number of terminals
  */
    void resize(count_t count);
};

}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_SUBSYSTEM_H_
