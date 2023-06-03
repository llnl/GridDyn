/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "../gridDynDefinitions.hpp"
#include "core/objectOperatorInterface.hpp"
#include "gmlc/containers/WorkQueue.hpp"
#include <future>
#include <memory>
#include <string>
#include <vector>

// limit violation definitions
#define NO_VIOLATION 0
#define VOLTAGE_OVER_LIMIT_VIOLATION 10
#define VOLTAGE_UNDER_LIMIT_VIOLATION 11
#define MVA_EXCEED_RATING_A 13
#define MVA_EXCEED_RATING_B 14
#define MVA_EXCEED_ERATING 15

#define MINIMUM_ANGLE_EXCEEDED 20
#define MAXIMUM_ANGLE_EXCEEDED 21
#define MINIMUM_CURRENT_EXCEEDED 30
#define MAXIMUM_CURRENT_EXCEEDED 31

#define CONVERGENCE_FAILURE 45

namespace griddyn {
class gridDynSimulation;

/** class encapsulating the data needed to record a violation */
class Violation {
  public:
    std::string m_objectName;  //!< the  name of the object with the violation
    double level{0.0};  //!< the value of the parameter exceeding some limit
    double limit{0.0};  //!< the limit value
    double percentViolation{100.0};  //!< the violation percent;
    int contingency_id{0};  //!< usually added later or ignored
    int violationCode;  //!< a code representing the type of violation
    int severity{0};  //!< a code indicating the severity of the violation
    Violation(const std::string& name = "", int code = 0): m_objectName(name), violationCode(code)
    {
    }
    /** @brief encode the violation to a string
    @return the violation string*/
    std::string to_string() const;
};

class Event;

enum class contingency_mode_t { N_1, N_1_1, N_2, N_2_LINE, N_3_LINE, line, gen, load, bus, custom, unknown };

class Contingency;
/** convert a string to a contingency mode*/
contingency_mode_t getContingencyMode(const std::string& mode);
/** class defining some extra optional info used for building contingency lists */
class extraContingencyInfo {
  public:
    std::shared_ptr<Contingency> baseCont;  //!< pointer to the base contingency
    double cutoff{0.0};  //!< the threshold level to trigger
    double delta{0.0};  //!< the change in data
    int stage{0};  //!< which stage should the contingency execute in
    bool simplified{false};  //!< indicator that should use simplifed output
    extraContingencyInfo() = default;
};

/** an object pointing to empty information*/
const extraContingencyInfo emptyExtraInfo{};
/** class that encapsulated the information about a contingency
 */
class Contingency: public gmlc::containers::basicWorkBlock, objectOperatorInterface {
  private:
    static std::atomic_int
        contingencyCount;  //!< static variable counting the number of created contingencies
  public:
    std::string name;  //!< contingency name
    int id;  //!< contingency id
    bool simplifiedOutput{false};  //!< indicator to use simplified output
    std::atomic<bool> completed{false};  //!< boolean indicator if the contingency was run

    std::vector<Violation> Violations;  //!< the resulting violations
    double PI{ 0.0 };  //!< performance index score
    double lowV{ 0.0 };  //!< minimum voltage
    std::vector<double> busVoltages;  //!< vector of bus voltages
    std::vector<double> busAngles;  //!< vector of bus Angles
    std::vector<double> Lineflows;  //!< vector of transmission line flows
    double preEventLoad{0.0};
    double preContingencyLoad{0.0}; //!< storage for original load to detect load loss
    double contingencyLoad{ 0.0 }; //!< the storage for the final load
    double preEventGen{0.0};
    double preContingencyGen{0.0}; //!< storage for original generation to detect generation loss
    double contingencyGen{ 0.0 }; //!< the storage for the final generation
    int islands{0}; //number of islands in the output
  protected:
    gridDynSimulation* gds = nullptr;  //!< master simulation object
    std::promise<int> promise_val;  //!< paired with future for asynchronous operation
    /// the future object to contain the data that will come upon execution
    std::shared_future<int>
        future_ret;
    /// events that describe the contingency
    std::vector<std::vector<std::shared_ptr<Event>>>
        eventList;  
  public:
    /** default constructor*/
    Contingency();
    /** construct from a sim and event*/
    Contingency(gridDynSimulation* sim, std::shared_ptr<Event> ge = nullptr);
    /** run the contingency
     */
    virtual void execute() override;

    virtual bool isFinished() const override;

    /** set the contingency root object
    @param[in] gdSim  a gridDynSimulation object that is the basis for the contingencies
    */
    void setContingencyRoot(gridDynSimulation* gdSim);
    /** add an event to a contingency
    @param[in] ge the new Event to add to the contingency
      @param[in] stage  the stage to execute the contingency
    */
    void add(std::shared_ptr<Event> ge, index_t stage = 0);

    /** merge two contingencies */
    void merge(const Contingency &c2,index_t stage=0);
    /**
    merge two contingencies if they are unige
    @return true if they were merged
    */
    bool mergeIfUnique(const Contingency &c2, index_t stage=0);

    /** generate a header string for a csv file including the data field names this
    @details this header line would be general to all similar contingencies
    */
    std::string generateHeader() const;

    /** generate the name and contingency as a string
    */
    std::string generateContingencyString() const;

    /** generate an output line for a csv file containing the contingency result data and any
     * violations
     */
    std::string generateFullOutputLine() const;
    /** generate an output string containing just the contingency and any violations
     *
     */
    std::string generateViolationsOutputLine() const;

    /** generate an output line based on internal settings*/
    std::string generateOutputLine() const;
    /** reset the contingency to be able to execute again*/
    void reset();
    /** wait for the contingency to finish executing*/
    void wait() const;
    /** Waits for the result to become available. Blocks until specified timeout_duration has elapsed or the result becomes available, whichever comes first. The return value identifies the state of the result.
    */
    std::future_status wait_for(std::chrono::milliseconds waitTime) const;

    coreObject* getObject() const override;

    void getObjects(std::vector<coreObject*>& objects) const override;

    void updateObject(coreObject* newObj,
                      object_update_mode mode = object_update_mode::match) override;

    std::shared_ptr<Contingency> clone(std::shared_ptr<Contingency> con = nullptr) const;
};
// Contingency execution functions
/** @brief build a list of contingencies
@param[in] contMode a string with the type of contingency analysis
@param[in] info extra information to pass to the contingency generator
@param[in] skip skip the first N contingencies
@return a vector of contingencies
*/
std::vector<std::shared_ptr<Contingency>>
    buildContingencyList(gridDynSimulation* gds,
                         const std::string& contMode,
                         const extraContingencyInfo& info = emptyExtraInfo, int skip=0);

/** @brief add a list of contingencies to an existing list of contingencies
 *@param[in] gds the simulation root object
@param[in] cmode a string with the type of contingency analysis
@param[out] contList the list of existing contingencies that
@param[in] info extra information to pass to the contingency generator
@param[in] skip skip the first N contingencies
@return the number of contingencies added to the list
*/
size_t buildContingencyList(gridDynSimulation* gds,
                            contingency_mode_t cmode,
                            std::vector<std::shared_ptr<Contingency>>& contList,
                            const extraContingencyInfo& info = emptyExtraInfo,    int skip=0);

/** @brief perform a contingency analysis
@param[in] contList the list of specific contingencies to test
@param[in] output a string containing the output specs (either fileName or some other string
@param[in] count the number of contingencies to run, 0 means all
*/
void runContingencyAnalysis(std::vector<std::shared_ptr<Contingency>>& contList,
                            const std::string& output, int count1=0, int count2=0);

}  // namespace griddyn
