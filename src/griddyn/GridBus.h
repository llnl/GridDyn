/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
#include "GridPrimary.h"
#include <queue>
#include <string>
#include <vector>

namespace griddyn {
// forward classes
class Link;
class GridLoad;
class Generator;

#define GOOD_SOLUTION (0)
#define QLIMIT_VIOLATION (1)
#define VLIMIT_VIOLATION (2)
#define PLIMIT_VIOLATION (3)

/** @brief Cached bus power balance on the system power base.
 * @details Loads and outward link flows are positive; generator injections are negative.
 * Call GridBus::updateLocalCache() before inspecting these values after a model change.
 */
class busPowers {
  public:
    double linkP = 0.0;  //!< [puMW] real power flowing out through links
    double loadP = 0.0;  //!< [puMW] real power coming from the loads
    double genP = 0.0;  //!< [puMW] real power from the generators

    double linkQ = 0.0;  //!< [pu]    reactive power coming from Links
    double loadQ = 0.0;  //!< [puMW]  reactive  power coming from Loads
    double genQ = 0.0;  //!< [puMW]  reactive power from the generators
    index_t seqID = 0;  //!< the sequence id of the latest state from which the powers are computed
    busPowers() {}
    /**reset all the powers to 0*/
    void reset();
    /** check if the busPowers needs an update based on the StateData*/
    bool needsUpdate(const StateData& stateDataValue) const;
    /** calculate the real power imbalance*/
    double sumP() const { return (linkP + loadP + genP); }
    /** calculate the reactive power imbalance*/
    double sumQ() const { return (linkQ + loadQ + genQ); }
};

/** @brief Fixed-voltage node and common interface for AC and DC buses.
 * @details Owns references to attached loads and generators and borrows attached links.
 * The base bus has no voltage/angle solver states; attached components may have states.
 * AcBus and DcBus supply the network balance equations. Bus outputs are ordered as
 * voltage [pu], angle [rad], and frequency [pu of the system frequency].
 * Numeric setters/getters accept explicit units. With default units, basevoltage is kV,
 * voltage is pu, angle is rad, and real/reactive powers are on the system power base.
 */
class GridBus: public GridPrimary {
  public:
    /** @brief flags for the buses*/

    static std::atomic<count_t> busCount;  //!<  counter for the buses
    static const int LOW_VOLTAGE_CHECK_FLAG = OBJECT_FLAG1;
    // afix is a fixed angle bus for power flow
    /** @brief Power-flow equations: bit 0 fixes angle and bit 1 fixes voltage. */
    enum class BusType : char {
        PQ = 0,  //!< a bus that defines the real and reactive power calculates V and theta
        AFIX = 1,  //!< a bus that defines the angle and reactive power calculates V and P
        PV = 2,  //!< a bus that defines P and V computes Q and angle
        SLK = 3  //!< a bus that defines V and theta and computes P & Q
    };
    /** @brief Dynamic bus equations, with the same fixed-variable bits as BusType. */
    enum class DynBusType : char {
        NORMAL = 0,  //!< a bus that computes V and theta
        FIX_ANGLE = 1,  //!< a bus that computes V and has a fixed theta
        FIX_VOLTAGE = 2,  //!< a bus that has a known voltage and computes theta
        DYN_SLK = 3  //!< a dynamic bus that knows both V and theta
    };
    // network is left as a public parameter since it has no impact on the calculations but is
    // useful for other object to define easily
    int32_t Network = 0;  //!<  the network a bus belongs to for labeling purposes
  protected:
    BusType type = BusType::PQ;  //!< [BusType] bus type: PV, PQ, or slack/swing
    DynBusType dynType =
        DynBusType::NORMAL;  //!< dynamic bus type normal, fixAngle, fixVoltage, dynSLK same types
    //! as for power flow but for dynamic simulations
    // 2 byte gap here
    double angle = 0.0;  //!< [rad]     voltage angle
    double voltage = 1.0;  //!< [puV]    per unit voltage magnitude
    double freq = 1.0;  //!<[puHz] estimated actual frequency
    busPowers S;  //!< storage for the power computation from the various sources;
    objVector<Generator*> attachedGens;  //!< list of the attached generators
    objVector<GridLoad*> attachedLoads;  //!<  list of all the loads
    objVector<Link*> attachedLinks;  //!< list of the attached links

    IOdata outputs;  //!< the current output values
    IOlocs outLocs;  //!< the current output locations

    // The inherited GridComponent base voltage is kept in volts for child models.
    model_parameter localBaseVoltage = 120.0;  //!< [kV] bus base voltage

    model_parameter Vtol =
        -1.0;  //!<[pu] voltage tolerance value <0 implies automatic setting from global levels
    model_parameter Atol =
        -1.0;  //!<[rad] angle tolerance  value <0 implies automatic setting from global levels

    CoreTime lowVtime = negTime;  //!< the last time a low voltage alert was triggered

  public:
    /** @brief default constructor*/
    explicit GridBus(const std::string& objName = "bus_$");
    /** @brief alternate constructor to specify voltage and angle
    @param[in] voltageStart the initial voltage [pu]
    @param[in] angleStart the initial angle [rad]
    @param[in] objName the object name
    */
    GridBus(double voltageStart, double angleStart, const std::string& objName = "bus_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    /** Persist this model's checkpoint fields; derived models must extend the hook. */
    void checkpointFields(CheckpointArchive& archive) override;
    // add components
    virtual void add(CoreObject* obj) override;
    /** @brief Attach a load and acquire an owning reference.
     * @pre loadObject is non-null and is not owned by another bus.
     * @details Sets the parent, base values, and load position. A repeated add of the same
     * object is a no-op. Retain a CoreOwningPtr if the load must outlive removal.
     */
    virtual void add(GridLoad* loadObject);
    /** @brief Attach a generator with the same ownership rules as add(GridLoad*). */
    virtual void add(Generator* gen);
    /** @brief Replace a load without changing the remaining load order.
     * @pre replacement is unattached, or is identical to previous (a no-op).
     * @details Acquires the replacement before returning and releases the old bus reference.
     * The old object may be destroyed; borrowed pointers to it must not be reused.
     * @throws ObjectAddFailure if previous is not attached, either pointer is null,
     * or replacement already has a parent.
     */
    virtual void replaceLoad(GridLoad* previous, GridLoad* replacement);
    /** @brief Replace a generator using the ownership contract of replaceLoad(). */
    virtual void replaceGenerator(Generator* previous, Generator* replacement);
    /** @brief Borrow a link reference without taking ownership.
     * @details Prefer Link::updateBus() to keep both terminals and bus lists consistent.
     */
    virtual void add(Link* lnk);

    // remove components
    virtual void remove(CoreObject* obj) override;
    /** @brief Detach a load, release ownership, and reindex remaining loads. */
    virtual void remove(GridLoad* loadObject);
    /** @brief Detach a generator, release ownership, and reindex remaining generators. */
    virtual void remove(Generator* gen);
    /** @brief  remove a Link object*/
    virtual void remove(Link* lnk);
    // deal with control alerts
    virtual void alert(CoreObject* obj, int code) override;

    // dynInitializeB
  protected:
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

  public:
    virtual ChangeCode powerFlowAdjust(const IOdata& inputs,
                                       std::uint32_t flags,
                                       CheckLevel level) override;  // only applicable in pFlow
    /** @brief Request a real-power adjustment [pu]; the base implementation is a no-op. */
    virtual void generationAdjust(double adjustment);
    virtual void reset(ResetLevels level = ResetLevels::MINIMAL) override;
    // dynInitializeB dynamics
  protected:
    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void disable() override;
    /** @brief Disconnect this bus and all attached links; set voltage and angle to zero. */
    virtual void disconnect() override;
    /** @brief  reconnect the bus
    @param[in] mapBus  a bus to pick of startup parameters from can be nullptr*/
    virtual void reconnect(GridBus* mapBus);
    virtual void reconnect() override;
    // parameter set functions
    /** @brief Legacy integer view of getType(); prefer the typed getter in new code. */
    int getBusType() const { return static_cast<int>(type); }
    /** @brief Apply a numeric setting to loads, generators, or all owned subobjects.
     * @details Supports load, gen/generator, secondary, and inherited all/sub/object groups.
     * Type-specific groups skip unsupported parameters; explicit units are forwarded.
     */
    virtual void setAll(std::string_view objtype,
                        std::string_view param,
                        double val,
                        units::unit unitType = units::defunit) override;
    virtual void getParameterStrings(stringVec& pstr,
                                     ParamStringType pstype = ParamStringType::all) const override;
    /** @brief Set connected or an inherited flag; true means connected for that flag. */
    virtual void setFlag(std::string_view flag, bool val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    // solver functions
    virtual void preEx(const IOdata& inputs,
                       const StateData& stateDataValue,
                       const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& stateDataValue,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
    /** do an update on the voltage similar to the algebraic update function but only looking at
    voltage
    @param[in] stateDataValue the state data to update
    @param[out] update the location to place the computed update
    @param[in] sMode the SolverMode associated with the state data
    @param[in] alpha the scale associated with the update
    */
    virtual void voltageUpdate(const StateData& stateDataValue,
                               double update[],
                               const SolverMode& sMode,
                               double alpha);

    /** @brief Local recovery hook; the fixed base bus performs no recovery iterations. */
    virtual void converge(CoreTime time,
                          double state[],
                          double dstateDt[],
                          const SolverMode& sMode,
                          ConvergeMode = ConvergeMode::HIGH_ERROR_ONLY,
                          double tol = 0.01) override;

    /** @brief Return the cached absolute real plus reactive power imbalance [pu]. */
    virtual double lastError() const;

    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  const SolverMode& sMode) override;

  public:
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    /** @brief a faster function to set the voltage and angle of a bus*
    @param[in] voltageNew  the new voltage
    @param[in] angleNew  the new angle
    */
    virtual void setVoltageAngle(double voltageNew, double angleNew);

    /** @brief function to propagate a network value to all connected buses
    @param[in] networkID the new network number
    @param[in,out] bstk  a queue containing the stack of buses that have yet to be checked
    */
    virtual void followNetwork(int networkID, std::queue<GridBus*>& bstk);
    /** @brief Check necessary local real-power capacity bounds.
     * @details Before power-flow initialization this returns true. Afterwards, connected,
     * enabled generation and link transfer ranges must bracket active demand within the
     * configured power-flow solver tolerance (simulation tolerance before solver creation,
     * or 1e-8 pu for a standalone bus). This is a local screen, not proof that the network
     * power flow will converge.
     */
    virtual bool checkCapable();
    // find components
    /** @brief find a link based on the bus desiring to be connected to
    @param[in] bus  the bus we want to check for a connecting link
    @return  a pointer to a Link that connects the current bus to the bus specified by bus or
    nullptr if none exists
    */
    Link* findLink(GridBus* bus) const;
    CoreObject* find(std::string_view objName) const override;
    CoreObject* getSubObject(std::string_view typeName, index_t num) const override;
    CoreObject* findByUserID(std::string_view typeName, index_t searchID) const override;

    Link* getLink(index_t x) const override;
    /**
    *@brief get a pointer for a particular GridLoad
    @param[in] x the index of the load being requested
    @return a pointer to the requested load or nullptr
    **/
    GridLoad* getLoad(index_t x = 0) const;
    /**
    *@brief get a pointer for a particular generator
    @param[in] x the index of the generator being requested
    @return a pointer to the requested generator or nullptr
    **/
    Generator* getGen(index_t x = 0) const;
    /** @brief Test for an unbranched active path using getDirectPath().
     * @param target destination bus or one of its attached generators/loads; null fails
     * @param source originating object or incoming link to exclude; may be null
     * @return false for a branch, dead end, disabled/disconnected path, or cycle
     */
    bool directPath(GridComponent* target, GridComponent* source = nullptr);
    /** @brief Follow exactly one onward enabled, connected link at each bus.
     * @details This is a controller-routing check, not a shortest-path search.
     * @param target destination bus or attached component; null returns an empty path
     * @param source originating object or incoming link; null starts the path with this bus
     * @return source (or this bus), traversed links in order, then target; empty on failure.
     * Returned pointers are borrowed. Intermediate buses are implicit in the link terminals.
     */
    std::vector<GridComponent*> getDirectPath(GridComponent* target,
                                              GridComponent* source = nullptr);
    /** @brief propagate and fix a power level for a bus
     * @param[in] makeSlack boolean indicating if the bus should be made into a slack bus or not
     * @return legacy adjustment status, which varies by bus/link implementation;
     * callers must not interpret it as a universal boolean success flag
     **/
    virtual int propogatePower(bool makeSlack = false);
    /** @brief get the bus type for power flow mode
     * @return the type of the bus
     **/
    BusType getType() const { return type; }
    /** @brief get the bus type for dynamic calculations
     * @return the type of the bus
     **/
    DynBusType getDynType() const { return dynType; }
    /** @brief get the bus voltage
     * @return the stored bus voltage [pu]
     **/
    double getVoltage() const { return voltage; }
    /** @brief get the bus angle
     * @return the stored bus angle [rad]
     **/
    double getAngle() const { return angle; }
    /** @brief get the bus frequency
     * @return the stored bus frequency [pu of system base frequency]
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

    /** @brief Return the real-generation limit [pu]; the base bus returns an unbounded sentinel. */
    virtual double getMaxGenReal() const { return kBigNum; }
    /** @brief Return the reactive-generation limit [pu]; the base bus returns an unbounded
     * sentinel. */
    virtual double getMaxGenReactive() const { return kBigNum; }

    double getLoadReal() const { return S.loadP; }
    /** @brief get the reactive power GridLoad
     * @return the reactive power GridLoad
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
    /** @brief Return available real-power increase or load reduction [pu].
     * @param time response horizon; maxTime requests unrestricted capacity
     * @details The base implementation returns zero; derived buses may aggregate controllers.
     */
    virtual double getAdjustableCapacityUp(CoreTime time = maxTime) const;
    /** @brief Return available real-power reduction or load increase [pu].
     * @param time response horizon; maxTime requests unrestricted capacity
     * @details The base implementation returns zero; derived buses may aggregate controllers.
     */
    virtual double getAdjustableCapacityDown(CoreTime time = maxTime) const;
    /** @brief the dPdf partial derivative  (may be deprecated in the future)
     * @return the @f$\frac{\partial P}{\partial f}@f$
     **/
    virtual double getdPdf() const { return 0; }
    /**@brief boolean indicator if the bus has inertial generators or loads*/
    bool hasInertialAngle() const;
    /** @brief get the tie error (may be deprecated in the future as this should be handled at an
     *area level)
     * @return the tie error
     **/
    virtual double getTieError() const { return kNullVal; }
    /** @brief Return the frequency-response contribution; the base implementation returns zero. */
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
                              const StateData& stateDataValue,
                              const SolverMode& sMode) const override;
    virtual IOlocs getOutputLocs(const SolverMode& sMode) const override;
    /** @brief Return no solver location for a fixed base-bus output. */
    virtual index_t getOutputLoc(const SolverMode& sMode, index_t num = 0) const override;

    /** @brief Borrow the voltage/angle/frequency cache from the last state-based update.
     * @details Call updateLocalCache(inputs, stateData, mode) first. The reference remains
     * valid for the bus lifetime, but its contents change on subsequent updates.
     */
    const IOdata& getOutputsRef() const;
    /** @brief Borrow locations from the last state-based cache update.
     * @details Prefer getOutputLocs(mode) when no cache update has been performed for that mode.
     */
    virtual const IOlocs& getOutputLocsRef() const;

    virtual double getOutput(const IOdata& inputs,
                             const StateData& stateDataValue,
                             const SolverMode& sMode,
                             index_t outNum = 0) const override;

    virtual double getOutput(index_t outNum = 0) const override;

    /** @brief get the voltage
    * @param[in] state the system state
    @param[in] sMode the corresponding SolverMode to the state
  @return the sbus voltage
    **/
    virtual double getVoltage(const double state[], const SolverMode& sMode) const;
    /** @brief get the angle
    * @param[in] state the system state
    @param[in] sMode the corresponding SolverMode to the state
    @return the bus angle
    **/
    virtual double getAngle(const double state[], const SolverMode& sMode) const;
    /** @brief get the voltage
    * @param[in] stateDataValue the system state data
    @param[in] sMode the corresponding SolverMode to the state data
    @return the bus voltage
    **/
    virtual double getVoltage(const StateData& stateDataValue, const SolverMode& sMode) const;
    /** @brief get the angle
    * @param[in] stateDataValue the system state data
    @param[in] sMode the corresponding SolverMode to the state
    @return the bus angle
    **/
    virtual double getAngle(const StateData& stateDataValue, const SolverMode& sMode) const;
    /** @brief get the bus frequency
    * @param[in] stateDataValue the system state data
    @param[in] sMode the corresponding SolverMode to the state
    @return the bus frequency
    **/
    virtual double getFreq(const StateData& stateDataValue, const SolverMode& sMode) const;

    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

    friend bool compareBus(GridBus* bus1, GridBus* bus2, bool cmpValues, bool printDiff);
    virtual void updateFlags(bool dynOnly = false) override;
    // for registering and removing power control objects

    // for dealing with buses merged with zero impedance link
    /** @brief  merge a bus with the calling bus*/
    virtual void mergeBus(GridBus* mbus);
    /** @brief  unmerge a bus with the calling bus*/
    virtual void unmergeBus(GridBus* mbus);
    /** @brief  check if all the buses that are merged should be*/
    virtual void checkMerge();

    /** @brief  register an object for voltage control on a bus*/
    virtual void registerVoltageControl(GridComponent* comp);
    /** @brief  remove an object from voltage control on a bus*/
    virtual void removeVoltageControl(GridComponent* comp);
    /** @brief  register an object for power control on a bus*/
    virtual void registerPowerControl(GridComponent* comp);
    /** @brief  remove an object from power control on a bus*/
    virtual void removePowerControl(GridComponent* comp);

    virtual const std::vector<stringVec>& outputNames() const override;
    units::unit outputUnits(index_t outputNum) const override;

  protected:
    /** @brief compute the current power-balance error
    @param[in] stateDataValue  the StateData to compute the error for
    @param[in] sMode the SolverMode corresponding to the stateData
    @return the error in the power balance equations
    */
    virtual double computeError(const StateData& stateDataValue, const SolverMode& sMode);

  private:
    template<class X>
    friend void addObject(GridBus* bus, X* obj, objVector<X*>& OVector);
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
bool compareBus(GridBus* bus1, GridBus* bus2, bool cmpValues = false, bool printDiff = false);

/** @brief find the matching bus in a different tree
  searches a cloned object tree to find the corresponding bus
@param[in] bus  the bus to search for
@param[in] src  the existing parent object
@param[in] sec  the desired parent object tree
@return a pointer to a bus on the sec tree that matches bus based on name and location
*/
GridBus* getMatchingBus(GridBus* bus, const GridPrimary* src, GridPrimary* sec);

}  // namespace griddyn
