/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreObject.h"
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::fmi {
class FmiEvent;
class FmiCollector;

/** class to manage the linkages from the FMI to the GridDyn objects*/
class FmiCoordinator: public griddyn::CoreObject {
  private:
    /** defining a small structure for containing inputs*/
    typedef struct {
        std::string name;
        FmiEvent* event;
    } InputSet;
    /** defining a small structure for containing outputs as members of a collector*/
    typedef struct {
        std::string name;
        int column;
        index_t outputIndex;
        FmiCollector* collector;
    } OutputSet;
    /** appropriate aliases*/
    using VrInputPair = std::pair<index_t, InputSet>;
    using VrOutputPair = std::pair<index_t, OutputSet>;

    std::vector<VrInputPair> mInputVr;  //!< container for the inputs
    std::vector<VrInputPair> mParamVr;  //!< container for the parameters
    std::vector<VrOutputPair> mOutputVr;  //!< container for the outputs
    std::vector<double> mOutputPoints;  //!< temporary storage for the outputs
    std::vector<FmiCollector*> mCollectors;  //!< storage for the collectors
    std::vector<std::shared_ptr<HelperObject>> mHelpers;  //!< storage to keep helper objects active
    std::atomic<index_t> mNextVr{10};  //!< atomic to maintain a vr counter
    std::mutex mHelperProtector;  //!< mutex lock to accept incoming helpers in a parallel system
    std::map<std::string, index_t>
        mVrNames;  //!< structure to store the names of all the valueReferences
  public:
    explicit FmiCoordinator(const std::string& name = "");

    /** register a new parameter
@param[in] paramName the name of the parameter
@param[in] eventObject a pointer to an event
*/
    void registerParameter(const std::string& paramName, FmiEvent* eventObject);

    /** register a new input
@param[in] inputName the name of the parameter
@param[in] eventObject a pointer to an event
*/
    void registerInput(const std::string& inputName, FmiEvent* eventObject);

    /** register a new output
@param[in] outputName the name of the parameter
@param[in] column the column index into the collector
@param[in] outputCollector a pointer to a collector
*/
    void registerOutput(const std::string& outputName, int column, FmiCollector* outputCollector);

    /** send a numerical input to the appropriate location
@param[in] valueReference the fmi Value Reference
@param[in] inputValue the numerical value to place
@return true if successful
*/
    bool sendInput(index_t valueReference, double inputValue);

    /** send a string input to the appropriate location
    @param[in] valueReference the fmi Value Reference
    @param[in] stringValue the string to place
    @return true if successful
    */
    bool sendInput(index_t valueReference, const char* stringValue);

    /** get a numerical output
@param[in] valueReference the fmi Value Reference
@return the numerical output
*/
    double getOutput(index_t valueReference);

    /** get the numerical outputs from the underlying simulation and store them to a local
buffer
@param[in] time the time to get the outputs for
*/
    void updateOutputs(coreTime time);

    /** get a string representing the FMIName of the current simulation*/
    const std::string& getFmiName() const;
    /** get a vector of the inputs*/
    const std::vector<VrInputPair>& getInputs() const { return mInputVr; }
    /** get a vector of the parameter object*/
    const std::vector<VrInputPair>& getParameters() const { return mParamVr; }
    /** get a vector of the parameter outputs*/
    const std::vector<VrOutputPair>& getOutputs() const { return mOutputVr; }
    /** locate value reference by name
@return kNullLocation if name is not found*/
    index_t findVR(const std::string& varName) const;

    virtual void addHelper(std::shared_ptr<HelperObject> helperObjectPtr) override;

    static bool isStringParameter(const VrInputPair& param);
};

}  // namespace griddyn::fmi

