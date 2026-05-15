/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridPrimary.h"
#include <queue>
#include <string>

namespace griddyn {
class gridBus;  // forward class definition

/** @brief structure containing information on the states of the joining buses
 included the voltages, angles and some common calculations used in calculating the flows
\f[
Vmx=\frac{v1*v2}{tap}
\f]
the seqID is also the index of the state data it was calculated from*/
class linkI {
  public:
    double v1 = 0.0;  //!< [puV] voltage at bus1
    double v2 = 0.0;  //!< [puV] voltage at bus2
    double theta1 = 0.0;  //!< [rad] angle at bus1
    double theta2 = 0.0;  //!< [rad] angle at bus2
    index_t seqID = 0;  //!< the current sequence id of the local data
};

/** @brief structure containing information on the flows for the link
        the seqID is also the index of the state data it was calculated from*/
class linkF {
  public:
    double P1 = 0.0;  //!< [puMW] power transferred from bus 1
    double P2 = 0.0;  //!< [puMW] power transferred from bus 2
    double Q1 = 0.0;  //!< [puMW] reactive power transferred from bus 1
    double Q2 = 0.0;  //!< [puMW] reactive power transferred from bus 2
    index_t seqID = 0;
};

/** @brief the basic class that links multiple nodes(buses) together
*  the base class for objects which connect other objects mainly buses
it implements a trivial transport model Q=0, P1=Pset P2=-(Pset-LossFraction*Pset)

Each link has a disconnect switch at the from bus and the to bus
*/
class Link: public gridPrimary {
  public:
    static std::atomic<count_t> linkCount;  //!< static variable counting the number of created
                                            //!< lines used for automatic user ID creation
    // it can be edited as it does not impact link operations just for user convenience
    /** @brief define some basic flag locations for Link*/
    enum gridLink_flags {
        switch1_open_flag = object_flag1,  //!<  switch for the from bus
        switch2_open_flag = object_flag2,  //!< switch for the to bus
        fixed_target_power = object_flag3,  //!< flag indicating if the power flow was fixed
        network_connected =
            object_flag4,  //!< indicates if a link ties the buses together in connected network

    };

  protected:
    gridBus* B1 = nullptr;  //!< the bus on the from side
    gridBus* B2 = nullptr;  //!< the bus on the to side

    index_t circuitNum = 1;  //!< helper field for multicircuit links
    linkI linkInfo;  //!< holder for the latest bus information
    linkF linkFlows;  //!< holder latest computed power flow information
    double ratingA = kBigNum;  //!< [puA] the long term rating of the link
    double ratingB = kBigNum;  //!< [puA] the short term rating of the link
    double Erating = kBigNum;  //!< [puA] the emergency rating of the link

    double Pset = 0;  //!< [puMW] the scheduled power of the link
    double lossFraction = 0;  //!<[%] the fraction of power transferred that is lost
  public:
    /** @brief default constructor*/
    explicit Link(const std::string& objName = "link_$");

    /** @brief virtual destructor*/
    virtual ~Link();
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void disable() override;
    /** @brief reconnect the link*/
    virtual void disconnect() override;

    virtual void reconnect() override;
    // handle end switches
    /** @brief get the switch state
     * @return true if switch is open, false if closed
     */
    virtual bool switchTest() const
    {
        return (opFlags[switch1_open_flag] || opFlags[switch2_open_flag]);
    }
    /** @brief get the switch state
    @param[in] num  the number of the switch 1 for "from" bus 2 for "to" bus
    * @return true if switch is open, false if closed
    */
    virtual bool switchTest(index_t num) const
    {
        return (num == 2) ? opFlags[switch2_open_flag] : opFlags[switch1_open_flag];
    }
    /** @brief set the switch state
    @param[in] num  the number of the switch 1 for "from" bus 2 for "to" bus
    @param[in] mode the mode of the switch true for open  false for closed
    */
    virtual void switchMode(index_t num, bool mode);
    /** @brief get the connectivity
     * @return true if there is a connection between the to and from bus
     */
    virtual bool isConnected() const override;

    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& stateData,
                                  const solverMode& sMode) override;

    /** @brief allow the real power flow to be fixed by adjusting the properties of one bus or
    another performs the calculations necessary to get the power at the measureTerminal to be a
    certain value
    @param[in] power  the desired real power flow as measured by measureTerminal
    @param[in] measureTerminal  the measure terminal-either a terminal number (1 or higher) or a
    busID,  1 by default
    @param[in] fixedTerminal -the terminal that doesn't change (terminal number or busID) if 0 both
    are changed or 1 is selected based on busTypes
    @param[in] unitType -- the units related to power
    @return 0 for success, some other number for failure
    */
    virtual int fixRealPower(double power,
                             id_type_t measureTerminal,
                             id_type_t fixedTerminal = 0,
                             units::unit unitType = units::defunit);
    /** @brief allow the power flow to be fixed by adjusting the properties of one bus or another
     performs the calculations necessary to get the power at the measureTerminal to be a certain
    value
    @param[in] rPower  the desired real power flow as measured by measureTerminal
    @param[in] qPower  the desired reactive power flow as measured by measureTerminal
    @param[in] measureTerminal  the measure terminal-either a terminal number (1 or higher) or a
    busID,  1 by default
    @param[in] fixedTerminal -the terminal that doesn't change (terminal number or busID) if 0 both
    are changed or 1 is selected based on busTypes
    @param[in] unitType -- the units related to power
    @return 0 for success, some other number for failure
    */
    virtual int fixPower(double rPower,
                         double qPower,
                         id_type_t measureTerminal,
                         id_type_t fixedTerminal = 0,
                         units::unit unitType = units::defunit);
    /** @brief propagate a network number to all connected buses
     checks if a link actually connects the two buses in an AC sense, then checks if a bus is
    already part of the specified network and if not it adds it to the queue
    @param[in] network  the new network number
    @param[in] stk a FIFO queue of buses to add to the network
    */
    virtual void followNetwork(int network, std::queue<gridBus*>& stk);
    /** @brief update one of the link terminals with a new bus
    @param[in] bus  the new bus to connect to
    @param[in] busNumber  1 for from,  2 for "to"
    */
    virtual void updateBus(gridBus* bus, index_t busNumber);
    /** @brief check for any violations of link limits or other factors based on power flow results
     checks things like the maximum angle,  power flow /current limits based on ratings and a few
    other things
    @param[out] Violation_vector --a list of all the violations any new violations get added to the
    result
    */
    virtual void pFlowCheck(std::vector<Violation>& Violation_vector) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    /** @brief do a quick update  (may be deprecated)
     * @return the power transfer
     */
    virtual double quickupdateP();
    /** @brief get the remaining capacity of the line
     * @return the remaining capacity
     */
    virtual double remainingCapacity() const;
    /** @brief get angle differential of the buses
     * @return the angle difference
     */
    virtual double getAngle() const;
    /** @brief get angle differential of the buses
    @param[in] state  the state of the system
    @param[in] sMode the solverMode the state is based on
    * @return the angle difference
    */
    virtual double getAngle(const double state[], const solverMode& sMode) const;
    /** @brief get the absolute angle of an attached bus
    @param[in] busId  either 1 or 2 to get the actual angle of the bus if the busID is used it gets
    the opposite angle
    * @return the absolute angle
    */
    double getBusAngle(id_type_t busId = 0) const;
    /** @brief get the absolute angle of an attached bus
    @param[in] busId  either 1 or 2 to get the actual angle of the bus if the busID is used it gets
    the opposite angle
    * @return the absolute angle
    */

    double
        getBusAngle(const stateData& stateData, const solverMode& sMode, id_type_t busId = 0) const;
    /** @brief get the voltage of an attached bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the voltage
    */
    virtual double getVoltage(id_type_t busId = 0) const;
    /** @brief get the real perceived impedance at a particular bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the perceived impedance
    */
    virtual double getRealImpedance(id_type_t busId = 0) const;
    /** @brief get the imaginary part of the perceived impedance at a particular bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the perceived imaginary component of the impedance
    */
    virtual double getImagImpedance(id_type_t busId = 0) const;
    /** @brief get the magnitude of the perceived impedance at a particular bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the magnitude of the perceived impedance, sign indicated flow direction of the real
    current
    */
    virtual double getTotalImpedance(id_type_t busId = 0) const;
    /** @brief get the magnitude of the current at a particular bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the magnitude of the current, sign indicated flow direction of the real current
    */
    virtual double getCurrent(id_type_t busId = 0) const;
    /** @brief get the angle of the current at a particular bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the angle of the current
    */
    virtual double getCurrentAngle(id_type_t busId = 0) const;
    /** @brief get the real part of the current at a particular bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the real part of the current
    */
    virtual double getRealCurrent(id_type_t busId = 0) const;
    /** @brief get the imaginary part of the current at a particular bus
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the imaginary part of the current
    */
    virtual double getImagCurrent(id_type_t busId = 0) const;
    /** @brief get the real power at a particular bus
     this function is to allow a link to act similarly to a gridSecondary
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the real Power
    */
    virtual double getRealPower(id_type_t busId = 0) const;
    /** @brief get the real power at a particular bus
     this function is to allow a link to act similarly to a gridSecondary
    @param[in] busId  either 1 or 2 or the object id of the bus
    * @return the real Power
    */
    virtual double getReactivePower(id_type_t busId = 0) const;
    /** @brief get the loss of a line
     * @return the loss in pu
     */
    virtual double getLoss() const;
    /** @brief get reactive power consumed by the link
     * @return the reactive power consumption
     */
    virtual double getReactiveLoss() const;
    /** @brief get the number of terminals
     * @return the number of terminals
     */
    virtual count_t terminalCount() const { return 2; }

    virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;
    /** @brief get the max transfer capacity
     * @return the max capacity
     */
    virtual double getMaxTransfer() const;

    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    gridBus* getBus(index_t busInd) const override;

    coreObject* getSubObject(std::string_view typeName, index_t num) const override;

    // test for various conditions of overload and automatically trip the line if needed
    virtual bool testAndTrip(int tripLevel);
    // dynInitializeB power flow
  protected:
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

  public:
    /** @brief check if two buses should be merged and the line effects ignored
     */
    virtual void checkMerge() {}
    // dynInitializeB dynamics
  protected:
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

  public:
    // for computing all the Jacobian elements at once
    using gridComponent::ioPartialDerivatives;
    virtual void ioPartialDerivatives(id_type_t busId,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode);

    using gridComponent::outputPartialDerivatives;
    virtual void outputPartialDerivatives(id_type_t busId,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode);

    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& stateData,
                              const solverMode& sMode) const override;
    virtual IOdata getOutputs(id_type_t busId, const stateData& sD, const solverMode& sMode) const;
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;

  protected:
    /** @brief deal with any effects of a switch change
    @param[in] switchNum  the number of the switch that changed
    */
    virtual void switchChange(int switchNum);

  private:
    /**function to do the power transfer computation*/
    void computePowers();
};
/** @brief find the matching link in a different tree
  searches a cloned object tree to find the corresponding link
@param[in] lnk  the link to search for
@param[in] src  the existing parent object
@param[in] sec  the desired parent object tree
@return a pointer to a link on the second tree that matches the calling link based on name and
location
*/
Link* getMatchingLink(Link* lnk, gridPrimary* src, gridPrimary* sec);

/** @brief compare 2 links
  check a number of link parameters to see if they match, probably not that useful of function any
more ,but it was useful during development
@param[in] lnk1  the first link to check
@param[in] lnk2  the second link to check
@param[in] cmpBus  whether to compare links or not  (deep comparison of links)
@param[in] printDiff  true if the diffs are to be printed
@return true if match
*/
bool compareLink(Link* lnk1, Link* lnk2, bool cmpBus = false, bool printDiff = false);

}  // namespace griddyn
