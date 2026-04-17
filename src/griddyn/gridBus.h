/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
// headers
#include "gridPrimary.h"
#include <queue>
#include <string>
#include <vector>

namespace griddyn {
// forward classes
class Link;
class Load;
class Generator;

#define GOOD_SOLUTION (0)
#define QLIMIT_VIOLATION (1)
#define VLIMIT_VIOLATION (2)
#define PLIMIT_VIOLATION (3)

/** @brief helper class for gridBus to store the power information
 */
class busPowers {
  public:
    double linkP = 0.0;  //!< [puMW]    reactive power coming from Links
    double loadP = 0.0;  //!< [puMW] real power coming from the loads
    double genP = 0.0;  //!< [puMW] real power from the generators

    double linkQ = 0.0;  //!< [pu]    reactive power coming from Links
    double loadQ = 0.0;  //!< [puMW]  reactive  power coming from Loads
    double genQ = 0.0;  //!< [puMW]  reactive power from the generators
    index_t seqID = 0;  //!< the sequence id of the latest state from which the powers are computed
    busPowers() {}
    /**reset all the powers to 0*/
    void reset();
    /** check if the busPowers needs an update based on the stateData*/
    bool needsUpdate(const stateData& sD) const;
    /** calculate the real power imbalance*/
    double sumP() const { return (linkP + loadP + genP); }
    /** calculate the reactive power imbalance*/
    double sumQ() const { return (linkQ + loadQ + genQ); }
};

/** @brief basic node in a power systems
  This Bus class is primarily a base Class for other types of buses, but it can be instantiated it
provides the basic management of subObjects and defines the interface.  It has no states and fixes
the voltage and angle at whatever it gets set to

*/
class gridBus: public gridPrimary {
  public:
    /** @brief flags for the buses*/

    static std::atomic<count_t> busCount;  //!<  counter for the buses
    static const int low_voltage_check_flag = object_flag1;
    // afix is a fixed angle bus for power flow
    /* @brief enumeration to define potential busTypes for power flow*/
    enum class busType : char {
        PQ = 0,  //!< a bus that defines the real and reactive power calculates V and theta
        afix = 1,  //!< a bus that defines the angle and reactive power calculates V and P
        PV = 2,  //!< a bus that defines P and V computes Q and angle
        SLK = 3  //!< a bus that defines V and theta and computes P & Q
    };
    /* @brief enumeration to define potential busTypes for dynamic calculations*/
    enum class dynBusType : char {
        normal = 0,  //!< a bus that computes V and theta
        fixAngle = 1,  //!< a bus that computes V and has a fixed theta
        fixVoltage = 2,  //!< a bus that has a known voltage and computes theta
        dynSLK = 3  //!< a dynamic bus that knows both V and theta
    };
    // network is left as a public parameter since it has no impact on the calculations but is
    // useful for other object to define easily
    int32_t Network = 0;  //!<  the network a bus belongs to for labeling purposes
  protected:
    busType type = busType::PQ;  //!< [busType] bus type: PV, PQ, or slack/swing
    dynBusType dynType =
        dynBusType::normal;  //!< dynamic bus type normal, fixAngle, fixVoltage, dynSLK same types
    //! as for power flow but for dynamic simulations
    // 2 byte gap here
    double angle = 0.0;  //!< [rad]     voltage angle
    double voltage = 1.0;  //!< [puV]    per unit voltage magnitude
    double freq = 1.0;  //!<[puHz] estimated actual frequency
    busPowers S;  //!< storage for the power computation from the various sources;
    objVector<Generator*> attachedGens;  //!< list of the attached generators
    objVector<Load*> attachedLoads;  //!<  list of all the loads
    objVector<Link*> attachedLinks;  //!< list of the attached links

    IOdata outputs;  //!< the current output values
    IOlocs outLocs;  //!< the current output locations

    model_parameter localBaseVoltage = 120.0;  //!< [kV]    base voltage level

    model_parameter Vtol =
        -1.0;  //!<[pu] voltage tolerance value <0 implies automatic setting from global levels
    model_parameter Atol =
        -1.0;  //!<[rad] angle tolerance  value <0 implies automatic setting from global levels

    coreTime lowVtime = negTime;  //!< the last time a low voltage alert was triggered

  public:
    /** @brief default constructor*/
    explicit gridBus(const std::string& objName = "bus_$");
    /** @brief alternate constructor to specify voltage and angle
    @param[in] voltageStart  the initial voltage
    @param[in] angleStart the initial angle
    */
    gridBus(double voltageStart, double angleStart, const std::string& objName = "bus_$");

    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    // add components
    virtual void add(coreObject* obj) override;
    /** @brief  add a Load object*/
    virtual void add(Load* ld);
    /** @brief  add a gridGenerator object*/
    virtual void add(Generator* gen);
    /** @brief  add a Link object*/
    virtual void add(Link* lnk);

    // remove components
    virtual void remove(coreObject* obj) override;
    /** @brief  remove a Load object*/
    virtual void remove(Load* ld);
    /** @brief  remove a Generator object*/
    virtual void remove(Generator* gen);
    /** @brief  remove a Link object*/
    virtual void remove(Link* lnk);
    // deal with control alerts
    virtual void alert(coreObject* obj, int code) override;

    // dynInitializeB
  protected:
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

  public:
    virtual change_code powerFlowAdjust(const IOdata& inputs,
                                        std::uint32_t flags,
                                        check_level_t level) override;  // only applicable in pFlow
    /** @brief  adjust the power levels of the contained adjustable secondary objects
    @param[in] adjustment the amount of the adjustment requested*/
    virtual void generationAdjust(double adjustment);
    virtual void reset(reset_levels level = reset_levels::minimal) override;
    // dynInitializeB dynamics
  protected:
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void disable() override;
    /** @brief  disconnect the bus*/
    virtual void disconnect() override;
    /** @brief  reconnect the bus
    @param[in] mapBus  a bus to pick of startup parameters from can be nullptr*/
    virtual void reconnect(gridBus* mapBus);
    virtual void reconnect() override;
    // parameter set functions
    virtual void setAll(const std::string& objtype,
                        const std::string& param,
                        double val,
                        units::unit unitType = units::defunit) override;
    virtual void getParameterStrings(stringVec& pstr,
                                     paramStringType pstype = paramStringType::all) const override;
    virtual void setFlag(const std::string& flag, bool val) override;
    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    // solver functions
    virtual void preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode) override;
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
    /** do an update on the voltage similar to the algebraic update function but only looking at
    voltage
    @param[in] sD the state data to update
    @param[out] update the location to place the computed update
    @param[in] sMode the solverMode associated with the state data
    @param[in] alpha the scale associated with the update
    */
    virtual void
        voltageUpdate(const stateData& sD, double update[], const solverMode& sMode, double alpha);

    virtual void converge(coreTime time,
                          double state[],
                          double dstate_dt[],
                          const solverMode& sMode,
                          converge_mode = converge_mode::high_error_only,
                          double tol = 0.01) override;

    /** @brief  return the last error in the real power*/
    virtual double lastError() const;

    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) override;

  public:
    void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    /** @brief a faster function to set the voltage and angle of a bus*
    @param[in] Vnew  the new voltage
    @param[in] Anew  the new angle
    */
    virtual void setVoltageAngle(double Vnew, double Anew);

    /** @brief function to propagate a network value to all connected buses
    @param[in] networkID the new network number
    @param[in,out] bstk  a queue containing the stack of buses that have yet to be checked
    */
    virtual void followNetwork(int networkID, std::queue<gridBus*>& bstk);
    /** @brief check if the bus is capable of operating
     */
    virtual bool checkCapable();
    // find components
    /** @brief find a link based on the bus desiring to be connected to
    @param[in] bs  the bus we want to check for a connecting link
    @return  a pointer to a Link that connects the current bus to the bus specified by bs or nullptr
    if none exists
    */
    Link* findLink(gridBus* bs) const;
    coreObject* find(const std::string& objName) const override;
    coreObject* getSubObject(const std::string& typeName, index_t num) const override;
    coreObject* findByUserID(const std::string& typeName, index_t searchID) const override;

    Link* getLink(index_t x) const override;
    /**
    *@brief get a pointer for a particular Load
    @param[in] x the index of the load being requested
    @return a pointer to the requested load or nullptr
    **/
    Load* getLoad(index_t x = 0) const;
    /**
    *@brief get a pointer for a particular generator
    @param[in] x the index of the generator being requested
    @return a pointer to the requested generator or nullptr
    **/
    Generator* getGen(index_t x = 0) const;
    /** @brief for identifying if there is a direct path from object to another in the system
     * @param target the object to search for
     * @param source the object from back up the path
     * @return whether there is a singular path to the object or not
     **/
    bool directPath(gridComponent* target, gridComponent* source = nullptr);
    /** @brief for obtaining the complete path to another object
     * @param target the object to search for
     * @param source the object from back up the path
     * @return a vector of objects with the path information  (NOTE: not necessarily the shortest
     *path)
     **/
    std::vector<gridComponent*> getDirectPath(gridComponent* target,
                                              gridComponent* source = nullptr);
    /** @brief propagate and fix a power level for a bus
     * @param[in] makeSlack boolean indicating if the bus should be made into a slack bus or not
     * @return value indicating success 0 on success -1 on failure
     **/
    virtual int propogatePower(bool makeSlack = false);
    /** @brief get the bus type for power flow mode
     * @return the type of the bus
     **/
    busType getType() const { return type; }
    /** @brief get the bus type for dynamic calculations
     * @return the type of the bus
     **/
    dynBusType getDynType() const { return dynType; }
    /** @brief get the bus voltage
     * @return the bus voltage
     **/
    double getVoltage() const { return voltage; }
    /** @brief get the bus angle
     * @return the bus angle
     **/
    double getAngle() const { return angle; }
    /** @brief get the bus frequency
     * @return the bus frequency
     **/
    double getFreq() const { return freq; }
    /** @brief get the bus real generation
     * @return the bus real generation
     **/
    double getGenerationReal() const { return S.genP; }
    /** @brief get the bus real generation as listed by the generators
    @details this only makes a difference for buses which do some automatic calculations in the
    power flow
    * @return the bus real generation
    **/
    double getGenerationRealNominal() const;
    /** @brief get the bus reactive generation
     * @return the bus reactive generation
     **/
    double getGenerationReactive() const { return S.genQ; }
    /** @brief get the bus reactive generation as listed by the generators
    @details this only makes a difference for buses which do some automatic calculations in the
    power flow
    * @return the bus real generation
    **/
    double getGenerationReactiveNominal() const;

    /** @brief get the maximumreal power generation
     * @return the maximum real power generation
     **/
    virtual double getMaxGenReal() const { return kBigNum; }
    /** @brief get the maximum reactive power generation
     * @return the maximum reactive power generation
     **/
    virtual double getMaxGenReactive() const { return kBigNum; }

    double getLoadReal() const { return S.loadP; }
    /** @brief get the reactive power Load
     * @return the reactive power Load
     **/
    double getLoadReactive() const { return S.loadQ; }
    /** @brief get the real power coming from links
     * @return the real link power
     **/
    double getLinkReal() const { return S.linkP; }
    /** @brief get the reactive power coming from links
     * @return the reactive link power
     **/
    double getLinkReactive() const { return S.linkQ; }
    /** @brief get the available controllable upward adjustments within a time period
    @ details this means power production or load reduction
    @param[in] time  the time period within which to do the adjustments
    * @return the reactive link power
    **/
    virtual double getAdjustableCapacityUp(coreTime time = maxTime) const;
    /** @brief get the available controllable upward adjustments within a time period
    @ details this means power production or load reduction
    @param[in] time  the time period within which to do the adjustments
    * @return the reactive link power
    **/
    virtual double getAdjustableCapacityDown(coreTime time = maxTime) const;
    /** @brief the dPdf partial derivative  (may be deprecated in the future)
     * @return the $\frac{\partial P}{\partial f}$
     **/
    virtual double getdPdf() const { return 0; }
    /**@brief boolean indicator if the bus has inertial generators or loads*/
    bool hasInertialAngle() const;
    /** @brief get the tie error (may be deprecated in the future as this should be handled at an
     *area level)
     * @return the tie error
     **/
    virtual double getTieError() const { return kNullVal; }
    /** @brief get the frequency response
     * @return the tie error
     **/
    virtual double getFreqResp() const;
    /** @brief get available regulation
     * @return the available regulation
     **/
    virtual double getRegTotal() const;
    /** @brief get the scheduled power
     * @return the scheduled power
     **/
    virtual double getSched() const;

    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& sD,
                              const solverMode& sMode) const override;
    virtual IOlocs getOutputLocs(const solverMode& sMode) const override;

    /** @brief get a const ref to the outputs*/
    const IOdata& getOutputsRef() const;
    /** @brief get a const ref to the output Locations
     */
    virtual const IOlocs& getOutputLocsRef() const;

    virtual double getOutput(const IOdata& inputs,
                             const stateData& sD,
                             const solverMode& sMode,
                             index_t outNum = 0) const override;

    virtual double getOutput(index_t outNum = 0) const override;

    /** @brief get the voltage
    * @param[in] state the system state
    @param[in] sMode the corresponding solverMode to the state
  @return the sbus voltage
    **/
    virtual double getVoltage(const double state[], const solverMode& sMode) const;
    /** @brief get the angle
    * @param[in] state the system state
    @param[in] sMode the corresponding solverMode to the state
    @return the bus angle
    **/
    virtual double getAngle(const double state[], const solverMode& sMode) const;
    /** @brief get the voltage
    * @param[in] sD the system state data
    @param[in] sMode the corresponding solverMode to the state data
    @return the bus voltage
    **/
    virtual double getVoltage(const stateData& sD, const solverMode& sMode) const;
    /** @brief get the angle
    * @param[in] sD the system state data
    @param[in] sMode the corresponding solverMode to the state
    @return the bus angle
    **/
    virtual double getAngle(const stateData& sD, const solverMode& sMode) const;
    /** @brief get the bus frequency
    * @param[in] sD the system state data
    @param[in] sMode the corresponding solverMode to the state
    @return the bus frequency
    **/
    virtual double getFreq(const stateData& sD, const solverMode& sMode) const;

    virtual void rootTest(const IOdata& inputs,
                          const stateData& sD,
                          double roots[],
                          const solverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& sMode) override;
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode,
                                  check_level_t level) override;

    friend bool compareBus(gridBus* bus1, gridBus* bus2, bool cmpValues, bool printDiff);
    virtual void updateFlags(bool dynOnly = false) override;
    // for registering and removing power control objects

    // for dealing with buses merged with zero impedance link
    /** @brief  merge a bus with the calling bus*/
    virtual void mergeBus(gridBus* mbus);
    /** @brief  unmerge a bus with the calling bus*/
    virtual void unmergeBus(gridBus* mbus);
    /** @brief  check if all the buses that are merged should be*/
    virtual void checkMerge();

    /** @brief  register an object for voltage control on a bus*/
    virtual void registerVoltageControl(gridComponent* comp);
    /** @brief  remove an object from voltage control on a bus*/
    virtual void removeVoltageControl(gridComponent* comp);
    /** @brief  register an object for power control on a bus*/
    virtual void registerPowerControl(gridComponent* comp);
    /** @brief  remove an object from power control on a bus*/
    virtual void removePowerControl(gridComponent* comp);

    virtual const std::vector<stringVec>& outputNames() const override;
    units::unit outputUnits(index_t outputNum) const override;

  protected:
    /** @brief compute the current power-balance error
    @param[in] sD  the stateData to compute the error for
    @param[in] sMode the solverMode corresponding to the stateData
    @return the error in the power balance equations
    */
    virtual double computeError(const stateData& sD, const solverMode& sMode);

  private:
    template<class X>
    friend void addObject(gridBus* bus, X* obj, objVector<X*>& OVector);
};

/** @brief compare 2 buses
  check a number of bus parameters to see if they match, probably not that useful of function any
more ,but it was useful during development
@param[in] bus1  bus1
@param[in] bus2 bus2
@param[in] cmpValues whether to compare values or not
@param[in] printDiff  true if the diffs are to be printed
@return true if match
*/
bool compareBus(gridBus* bus1, gridBus* bus2, bool cmpValues = false, bool printDiff = false);

/** @brief find the matching bus in a different tree
  searches a cloned object tree to find the corresponding bus
@param[in] bus  the bus to search for
@param[in] src  the existing parent object
@param[in] sec  the desired parent object tree
@return a pointer to a bus on the sec tree that matches bus based on name and location
*/
gridBus* getMatchingBus(gridBus* bus, const gridPrimary* src, gridPrimary* sec);

}  // namespace griddyn
