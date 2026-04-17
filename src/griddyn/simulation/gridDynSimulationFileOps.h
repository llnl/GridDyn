#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "../solvers/solverMode.hpp"
#include <memory>
#include <string>
#include <vector>
template<class Y>
class matrixData;

namespace griddyn {
// forward declare the template class matrixData

class solverMode;
class gridDynSimulation;

/** @brief save the current state to a file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to save the state to
*/
void saveState(gridDynSimulation* gds,
               const std::string& fileName,
               const solverMode& sMode = cEmptySolverMode,
               bool append = false);

/** @brief save the bus data to a csv file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to save the bus data to
*/
void saveBusData(gridDynSimulation* gds, const std::string& fileName);

/** @brief save the line data to a csv file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to save the line data to
*/
void saveLineData(gridDynSimulation* gds, const std::string& fileName);

/** @brief save the current state to an XML file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to save the state to
*/
void saveStateXML(gridDynSimulation* gds,
                  const std::string& fileName,
                  const solverMode& sMode = cEmptySolverMode);

/** @brief save the current state to a binary file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to save the state to
@param[in] sMode the solverMode to save the state
@param[in] append  boolean indicating the file should be appended
*/
void saveStateBinary(gridDynSimulation* gds,
                     const std::string& fileName,
                     const solverMode& sMode = cEmptySolverMode,
                     bool append = true);

/** @brief load a state vector from a file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadState(gridDynSimulation* gds,
               const std::string& fileName,
               const solverMode& sMode = cEmptySolverMode);

/** @brief load a binary state file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadStateBinary(gridDynSimulation* gds,
                     const std::string& fileName,
                     const solverMode& sMode = cEmptySolverMode);

/** @brief load a state vector from an XML file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadStateXML(gridDynSimulation* gds,
                  const std::string& fileName,
                  const solverMode& sMode = cEmptySolverMode);

/** @brief capture a Jacobian and a state to a file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
@param[in] sMode the solverMode to get the state from
*/
void captureJacState(gridDynSimulation* gds,
                     const std::string& fileName,
                     const solverMode& sMode = cEmptySolverMode);

/** @brief capture the Jacobian data to a file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
@param[in] sMode the solverMode to get the state from
*/
void saveJacobian(gridDynSimulation* gds,
                  const std::string& fileName,
                  const solverMode& sMode = cEmptySolverMode);

/** @brief save the powerflow results to a file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlow(gridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a cdf formatted file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowCdf(gridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to an XML file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowXML(gridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a formatted Text file
 this creates a text file for human readability not machine readability
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowTXT(gridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a binary file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowBinary(gridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a csv file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowCSV(gridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from a file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlow(gridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from an XML file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowXML(gridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from an CDF file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowCdf(gridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from a binary file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowBinary(gridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from a csv file
@param[in] gds  the gridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowCSV(gridDynSimulation* gds, const std::string& fileName);

/** struct containing binary Data information
 */
struct dataInfo {
    coreTime time = 0.0;
    std::uint32_t code = 0;
    std::uint32_t index = 0;
    std::uint32_t key = 0;
    std::uint32_t numElements = 0;
};

#define STATE_INFORMATION (0)
#define DERIVATIVE_INFORMATION (1)
#define RESIDUAL_INFORMATION (2)
/** @brief write a vector (state, resid, deriv, etc) to a file
encodes a header into the file
time(8 bytes), code(4 bytes), index(4 bytes), key(4 bytes), length(4 bytes),
then write data (NumElements*8 Bytes);

@param[in] time the time associated with the data
@param[in] index an indexing value associated with the data
@param[in] code a code describing the type of information
0 -state information
1 -derivative information
2 -residual information
@param[in] key a code indicating the source of the information (typically the index of the solver
data object)
@param[in] numElements the number of elements
@param[in] data the data to write to the file
@param[in] fileName the name of the file
@param[in] append indicator if the file should be appended or overwritten(def true)
@throw fileOperationError if the file cannot be opened
*/

void writeVector(coreTime time,
                 std::uint32_t code,
                 std::uint32_t index,
                 std::uint32_t key,
                 std::uint32_t numElements,
                 const double* data,
                 const std::string& fileName,
                 bool append = true);

/** @brief write a array to a file
encodes a header into the file
time(8 bytes), code(0x0001|code) for Jacobian data (4 bytes), index(4 bytes), key(4 bytes), length(4
bytes), then write data in triplets (4byte row, 4 byte col, 8 byte double data)
@param[in] time the time associated with the data
@param[in] code a code describing the type of information in the array
@param[in] index an indexing value associated with the data
@param[in] key a code indicating the source of the information (typically the index of the solver
data object)
@param[in] a1 the Jacobian data to write to the file
@param[in] fileName the name of the file
@param[in] append indicator if the file should be appended or overwritten(def true)
@throw fileOperationError if the file cannot be opened
*/

#define JACOBIAN_DATA 1
void writeArray(coreTime time,
                std::uint32_t code,
                std::uint32_t index,
                std::uint32_t key,
                matrixData<double>& a1,
                const std::string& fileName,
                bool append = true);

class Contingency;

/** write a contingency list to a text based file output
@param[in] contList the vector of contingencies that have been run
@param[in] fileName the name of the file to save
@param[in] count the number of contingencies to save
*/
void saveContingencyOutput(const std::vector<std::shared_ptr<Contingency>>& contList,
                           const std::string& fileName,
                           int count = 0);

}  // namespace griddyn
