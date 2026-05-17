/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
#include "gridPrimary.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
// forward classes
class gridDynSimulation;
class Relay;
class Link;
class gridBus;
class Generator;
class Source;
class coreObjectList;
class listMaintainer;

/** @brief class implementing a power system area
 the area class acts as a container for other primary objects including areas
it also acts as focal point for wide area controls such as AGC and can compute other functions and
statistics across a wide area
*/
class Area: public gridPrimary {
    friend class listMaintainer;

  public:
    /** @brief flags for area operations and control*/
    enum area_flags {
        reverse_converge = object_flag1,  //!< flag indicating that the area should do a
                                          //!< convergence/algebraic loop in reverse
        direction_oscillate =
            object_flag2,  //!< flag indicating that the direction of iteration for convergence
        //!< functions should flip every time the function is called
    };

  private:
    std::vector<gridBus*> m_Buses;  //!< list of buses contained in a the area
    std::vector<Link*> m_Links;  //!< links completely inside the area
    std::vector<Link*> m_externalLinks;  //!< links going to other areas
    std::vector<Area*> m_Areas;  //!< list of the areas contained within the parent area
    std::vector<Relay*> m_Relays;  //!< list of relay objects

    std::vector<gridPrimary*> primaryObjects;  //!< list of all the primary objects in the area
    // this is done to break apart the headers
    std::unique_ptr<coreObjectList> obList;  // a search index for object names

    std::vector<gridPrimary*> rootObjects;  //!< list of objects with roots
    std::vector<gridPrimary*> pFlowAdjustObjects;  //!< list of objects with power flow checks
    /** @brief storage location for shared_ptrs to griddyn
    the direct pointer to the object will get passed to the system but the ownership will be changed
    so it won't be deleted by the normal means this allows storage of shared_ptrs to modeled objects
    but also other objects that potentially act as storage containers, do periodic updates, generate
    alerts or interact with other simulations
    */
    std::vector<CoreObject*> objectHolder;  //!< storage location for shared pointers to an object

    // std::vector<Source *> signalsSources;    //!< sources for the area outputs

    std::unique_ptr<listMaintainer> opObjectLists;  //!<
    double fTarget = 1.0;  //!<[puHz] a target frequency
    int masterBus = -1;  //!< the master bus for frequency calculations purposes

  public:
    /** @brief the default constructor*/
    explicit Area(const std::string& objName = "area_$");
    /** @brief the default destructor*/
    virtual ~Area();

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void updateObjectLinkages(CoreObject* newRoot) override;
    // add components
    virtual void add(CoreObject* obj) override;
    /** @brief add a bus to the area
    @param[in] bus  the bus to add
    @throw objectAddFailure on add failure typically duplicated names
    */
    virtual void add(gridBus* bus);
    /** @brief add a link to the area
    @param[in] lnk  the link to add
    @throw objectAddFailure on add failure typically duplicated names
    */
    virtual void add(Link* lnk);
    /** @brief add an area to the area
    @param[in] area  the area to add
    @throw objectAddFailure on add failure typically duplicated names
    */
    virtual void add(Area* area);
    /** @brief add a relay to the area
    @param[in] relay  the relay to add
   @throw objectAddFailure on add failure typically duplicated names
    */
    virtual void add(Relay* relay);

    // remove components
    virtual void remove(CoreObject* obj) override;
    /** @brief remove a bus from the area
    @param[in] bus  the bus to remove

    */
    virtual void remove(gridBus* bus);
    /** @brief remove a link from the area
    @param[in] lnk  the link to remove

    */
    virtual void remove(Link* lnk);
    /** @brief remove an area from the area
    @param[in] area  the area to remove

    */
    virtual void remove(Area* area);
    /** @brief remove a relay from the area
    @param[in] relay  the relay to remove
    */
    virtual void remove(Relay* relay);

    // get component models
    virtual gridBus* getBus(index_t x) const override;
    virtual Link* getLink(index_t x) const override;
    virtual Area* getArea(index_t x) const override;
    virtual Relay* getRelay(index_t x) const override;
    /** @brief get a generator by index number
     this is kind of an ugly function but needed for some applications to search through all buses
    @param[in] x  the index of the generator to search for
    @return a point to the generator or nullptr
    */
    virtual Generator* getGen(index_t x);  //
    // dynInitializeB

    virtual void setOffsets(const solverOffsets& newOffsets, const solverMode& sMode) override;
    virtual void setOffset(index_t offset, const solverMode& sMode) override;

    virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;

    virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

    virtual std::pair<count_t, count_t> LocalRootCount(const solverMode& sMode) const override;

    virtual void loadStateSizes(const solverMode& sMode) override;

    virtual void loadJacobianSizes(const solverMode& sMode) override;

    virtual void loadRootSizes(const solverMode& sMode) override;
    virtual void setRootOffset(index_t Roffset, const solverMode& sMode) override;

  protected:
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

    // dynInitializeB dynamics
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    // TODO(phlpt): Implement this or remove it if angle updates are no longer used.
    /** @brief update the angles may be deprecated
    @param[in] time the time to update to
    */
    virtual void updateTheta(coreTime time);

    // parameter set functions
    virtual void setFlag(std::string_view flag, bool val) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void getParameterStrings(stringVec& pstr,
                                     paramStringType pstype = paramStringType::all) const override;
    void setAll(std::string_view type,
                std::string_view param,
                double val,
                units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    /** @brief determine if an object is already a member of the area
    @param[in] object  the object to check
    @return true if the object is a member false if not
    */
    virtual bool isMember(const CoreObject* object) const;
    // find components
    virtual CoreObject* find(std::string_view objName) const override;
    virtual CoreObject* getSubObject(std::string_view typeName, index_t num) const override;
    virtual CoreObject* findByUserID(std::string_view typeName, index_t searchID) const override;
    // solver functions

    virtual void alert(CoreObject* obj, int code) override;

    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix = "") const override;
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

    virtual void delayedResidual(const IOdata& inputs,
                                 const stateData& sD,
                                 double resid[],
                                 const solverMode& sMode) override;
    virtual void delayedDerivative(const IOdata& inputs,
                                   const stateData& sD,
                                   double deriv[],
                                   const solverMode& sMode) override;
    virtual void delayedJacobian(const IOdata& inputs,
                                 const stateData& sD,
                                 matrixData<double>& md,
                                 const IOlocs& inputLocs,
                                 const solverMode& sMode) override;
    virtual void delayedAlgebraicUpdate(const IOdata& inputs,
                                        const stateData& sD,
                                        double update[],
                                        const solverMode& sMode,
                                        double alpha) override;

    virtual change_code
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level) override;
    virtual void pFlowCheck(std::vector<Violation>& Violation_vector) override;
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
    // for identifying which variables are algebraic vs differential
    virtual void getVariableType(double sdata[], const solverMode& sMode) override;
    virtual void getTols(double tols[], const solverMode& sMode) override;
    // dynamic simulation
    virtual void guessState(coreTime time,
                            double state[],
                            double dstate_dt[],
                            const solverMode& sMode) override;

    /** @brief try to do a local converge on the solution
     to be replaced by the algebraic update function soon
    @param[in] time the time
    @param[in,out] state the system state
    @param[in,out] dstate_dt the system state derivative
    @param[in] sMode  the solverMode corresponding to the state
    @param[in]  mode the mode to do the convergence
    @param[in] tol  the tolerance to converge to

    */
    virtual void converge(coreTime time,
                          double state[],
                          double dstate_dt[],
                          const solverMode& sMode,
                          converge_mode mode,
                          double tol) override;
    virtual void updateLocalCache() override;

    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) override;

    virtual void reset(reset_levels level) override;
    // root finding functions
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
    // grab information
    /** @brief get a vector of voltage from the attached buses
    @param[out] voltages the vector to put the bus voltages
    @param[in] start  the index into the vector V to start the voltage states from this area
    @return an index where the last value was placed
    */
    count_t getVoltage(std::vector<double>& voltages, index_t start = 0) const;
    /** @brief get a vector of voltage from the attached buses
    @param[out] voltages the vector to put the bus  voltages
    @param[in] state  the system state
    @param[in] sMode the solverMode corresponding to the states
    @param[in] start  the index into the vector V to start the voltage states from this area
    @return an index where the last value was placed
    */
    count_t getVoltage(std::vector<double>& voltages,
                       const double state[],
                       const solverMode& sMode,
                       index_t start = 0) const;
    /** @brief get a vector of angles from the attached buses
    @param[out] angles the vector to put the bus  angles
    @param[in] start  the index into the vector V to start the angle states from this area
    @return an index where the last value was placed
    */
    count_t getAngle(std::vector<double>& angles, index_t start = 0) const;

    /** @brief get a vector of frequencies from the attached buses
    @param[out] frequencies the vector to put the bus  angles
    @param[in] start  the index into the vector V to start the angle states from this area
    @return an index where the last value was placed
    */
    count_t getFreq(std::vector<double>& frequencies, index_t start = 0) const;

    /** @brief get a vector of angles from the attached buses
    @param[out] angles the vector to put the bus angles
    @param[in] state  the system state
    @param[in] sMode the solverMode corresponding to the states
    @param[in] start  the index into the vector V to start the angle states from this area
    @return an index where the last value was placed
    */
    count_t getAngle(std::vector<double>& angles,
                     const double state[],
                     const solverMode& sMode,
                     index_t start = 0) const;
    /** @brief get a vector of real power from the attached links
    @param[out] powers the vector to put the link real powers
    @param[in] start  the index into the vector V to start the real power values
    @param[in] busNumber the bus index of the link to pick off the powers
    @return an index where the last value was placed
    */
    count_t
        getLinkRealPower(std::vector<double>& powers, index_t start = 0, int busNumber = 1) const;
    /** @brief get a vector of reactive power from the attached links
    @param[out] powers the vector to put the link reactive powers
    @param[in] start  the index into the vector V to start the reactive power values
    @param[in]  busNumber the bus index of the link to pick off the powers
    @return an index where the last value was placed
    */
    count_t getLinkReactivePower(std::vector<double>& powers,
                                 index_t start = 0,
                                 int busNumber = 1) const;
    /** @brief get a vector of losses of the attached links
    @param[out] losses the vector to put the losses
    @param[in] start  the index into the vector V to start the angle states from this area
    @return an index where the last value was placed
    */
    count_t getLinkLoss(std::vector<double>& losses, index_t start = 0) const;
    /** @brief get a vector of generation power from the attached buses
    @param[out] powers the vector to put the bus real power from generators
    @param[in] start  the index into the vector A to start the generation power values from this
    area
    @return an index where the last value was placed
    */
    count_t getBusGenerationReal(std::vector<double>& powers, index_t start = 0) const;
    /** @brief get a vector of generation reactive power from the attached buses
    @param[out] powers the vector to put the bus reactive power from generators
    @param[in] start  the index into the vector A to start the generation power values from this
    area
    @return an index where the last value was placed
    */
    count_t getBusGenerationReactive(std::vector<double>& powers, index_t start = 0) const;
    /** @brief get a vector of bus load power from the attached buses
    @param[out] powers the vector to put the bus load real power from bus loads
    @param[in] start  the index into the vector A to start the load power values from this area
    @return an index where the last value was placed
    */
    count_t getBusLoadReal(std::vector<double>& powers, index_t start = 0) const;
    /** @brief get a vector of bus load reactive power from the attached buses
    @param[out] powers the vector to put the bus load reactive power from bus loads
    @param[in] start  the index into the vector A to start the load reactive power values from this
    area
    @return an index where the last value was placed
    */
    count_t getBusLoadReactive(std::vector<double>& powers, index_t start = 0) const;
    /** @brief get a vector of bus names
    @param[out] names the vector to put the bus names
    @param[in] start  the index into the vector nm to start the area bus names
    @return an index where the last value was placed
    */
    count_t getBusName(stringVec& names, index_t start = 0) const;
    /** @brief get a vector of link names
    @param[out] names the vector to put the link names
    @param[in] start  the index into the vector nm to start the area link names
    @return an index where the last value was placed
    */
    count_t getLinkName(stringVec& names, index_t start = 0) const;
    /** @brief get a vector of buses attached to the area links
    @param[out] names the vector to put the bus names
    @param[in] start  the index into the vector nm to start the area link names
    @param[in] busNumber  the side of the link to get the bus names for
    @return an index where the last value was placed
    */
    count_t getLinkBus(stringVec& names, index_t start = 0, int busNumber = 0) const;

    /** @brief get the total adjustable CapacityUp for the area within a certain time frame
    @param[in] time  the time within which to make the adjustment
    @return athe total adjustable capacity Up
    */
    double getAdjustableCapacityUp(coreTime time = maxTime) const;
    /** @brief get the total adjustable Capacity Down for the area within a certain time frame
    @param[in] time  the time within which to make the adjustment
    @return athe total adjustable capacity Down
    */
    double getAdjustableCapacityDown(coreTime time = maxTime) const;
    /** @brief get the total loss for contained links
    @return the total area loss
    */
    double getLoss() const;
    /** @brief get the total area real generation
    @return the total area real generation
    */
    double getGenerationReal() const;
    /** @brief get the total area reactive generation
    @return the total area reactive Generation
    */
    double getGenerationReactive() const;
    /** @brief get the total area real load power
    @return the real Load power for the area
    */
    double getLoadReal() const;
    /** @brief get the total area reactive load power
    @return the reactive Load power for the area
    */
    double getLoadReactive() const;
    /** @brief get the average angle for the area
    @return the average angle
    */
    double getAvgAngle() const;
    /** @brief get the average angle for the area
    @param[in] sD the state data
    @param[in] sMode the solverMode corresponding to the state data
    @return the average angle
    */
    double getAvgAngle(const stateData& sD, const solverMode& sMode) const;

    /** @brief get the average frequency for the area
    @return the average frequency
    */
    double getAvgFreq() const;

    /** @brief get the total tie line flows into/out of the area
    @return the total tie line flows
    */
    double getTieFlowReal() const;
    /** flag all the voltage states
     * get a vector with an indicator of voltage states
     *@param[out] vStates a vector with a value of 1.0 for all voltage states and 0 otherwise
     *
     */
    void getVoltageStates(double vStates[], const solverMode& sMode) const;
    void getAngleStates(double aStates[], const solverMode& sMode) const;
    double getMasterAngle(const stateData& sD, const solverMode& sMode) const;
    virtual void updateFlags(bool dynOnly = false) override;
    /** @brief  get a vector of all the buses of the area
    @param[out] busVector  a vector of buses
    @param[in] start  the index to start placing the bus pointers
    @return the total number of buses placed start+busCount
    */
    count_t getBusVector(std::vector<gridBus*>& busVector, index_t start = 0) const;

    /** @brief  get a vector of all the links of the area
    @param[out] linkVector  a vector of links
    @param[in] start  the index to start placing the link pointers
    @return the total number of links placed start+busCount
    */
    count_t getLinkVector(std::vector<Link*>& linkVector, index_t start = 0) const;

  private:
    static std::atomic<count_t> areaCounter;  //!< basic counter for the areas to compute an id

    template<class X>
    friend void addObject(Area* area, X* obj, std::vector<X*>& objVector);

    template<class X>
    friend void removeObject(Area* area, X* obj, std::vector<X*>& objVector);
};

/** @brief find the matching area in a different tree
  searches a cloned object tree to find the corresponding bus
@param[in] area  the area to search for
@param[in] src  the existing parent object
@param[in] sec  the desired parent object tree
@return a pointer to an area on the second tree that matches the area based on name and location
*/
Area* getMatchingArea(Area* area, gridPrimary* src, gridPrimary* sec);

}  // namespace griddyn

