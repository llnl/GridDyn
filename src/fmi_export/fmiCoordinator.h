/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "core/coreObject.h"
#include <map>
#include <mutex>

namespace griddyn {
namespace fmi {
    class fmiEvent;
    class fmiCollector;

    /** class to manage the linkages from the FMI to the GridDyn objects*/
    class fmiCoordinator: public griddyn::coreObject {
      private:
        /** defining a small structure for containing inputs*/
        typedef struct {
            std::string name;
            fmiEvent* evnt;
        } inputSet;
        /** defining a small structure for containing outputs as members of a collector*/
        typedef struct {
            std::string name;
            int column;
            index_t outIndex;
            fmiCollector* col;
        } outputSet;
        /** appropriate aliases*/
        using vrInputPair = std::pair<index_t, inputSet>;
        using vrOutputPair = std::pair<index_t, outputSet>;

        std::vector<vrInputPair> inputVR;  //!< container for the inputs
        std::vector<vrInputPair> paramVR;  //!< container for the parameters
        std::vector<vrOutputPair> outputVR;  //!< container for the outputs
        std::vector<double> outputPoints;  //!< temporary storage for the outputs
        std::vector<fmiCollector*> collectors;  //!< storage for the collectors
        std::vector<std::shared_ptr<helperObject>>
            helpers;  //!< storage to keep helper objects active
        std::atomic<index_t> nextVR{10};  //!< atomic to maintain a vr counter
        std::mutex helperProtector;  //!< mutex lock to accept incoming helpers in a parallel system
        std::map<std::string, index_t>
            vrNames;  //!< structure to store the names of all the valueReferences
      public:
        explicit fmiCoordinator(const std::string& name = "");

        /** register a new parameter
    @param[in] paramName the name of the parameter
    @param[in] evnt a pointer to an event
    */
        void registerParameter(const std::string& paramName, fmiEvent* evnt);

        /** register a new input
    @param[in] inputName the name of the parameter
    @param[in] evnt a pointer to an event
    */
        void registerInput(const std::string& inputName, fmiEvent* evnt);

        /** register a new output
    @param[in] outputName the name of the parameter
    @param[in] column the column index into the collector
    @param[in] out a pointer to a collector
    */
        void registerOutput(const std::string& outputName, int column, fmiCollector* out);

        /** send a numerical input to the appropriate location
    @param[in] vr the fmi Value Reference
    @param[in] val the numerical value to place
    @return true if successful
    */
        bool sendInput(index_t vr, double val);

        /** send a string input to the appropriate location
    @param[in] vr the fmi Value Reference
    @param[in] val the string to place
    @return true if successful
    */
        bool sendInput(index_t vr, const char* s);

        /** get a numerical output
    @param[in] vr the fmi Value Reference
    @return the numerical output
    */
        double getOutput(index_t vr);

        /** get the numerical outputs from the underlying simulation and store them to a local
    buffer
    @param[in] time the time to get the outputs for
    */
        void updateOutputs(coreTime time);

        /** get a string representing the FMIName of the current simulation*/
        const std::string& getFMIName() const;
        /** get a vector of the inputs*/
        const std::vector<vrInputPair>& getInputs() const { return inputVR; }
        /** get a vector of the parameter object*/
        const std::vector<vrInputPair>& getParameters() const { return paramVR; }
        /** get a vector of the parameter outputs*/
        const std::vector<vrOutputPair>& getOutputs() const { return outputVR; }
        /** locate value reference by name
    @return kNullLocation if name is not found*/
        index_t findVR(const std::string& varName) const;

        virtual void addHelper(std::shared_ptr<helperObject> ho) override;

        static bool isStringParameter(const vrInputPair& param);
    };

}  // namespace fmi
}  // namespace griddyn
