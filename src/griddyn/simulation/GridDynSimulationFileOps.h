/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../solvers/SolverMode.hpp"
#include <memory>
#include <string>
#include <vector>
template<class Y>
class MatrixData;

namespace griddyn {
// forward declare the template class MatrixData

class SolverMode;
class GridDynSimulation;

/** @brief One binary solver-state snapshot, as recorded by saveStateBinary(). */
struct BinaryStateData {
    CoreTime time = timeZero;  //!< simulation time at capture
    SolverMode mode = cEmptySolverMode;  //!< recorded solver mode
    std::vector<double> state;
    std::vector<double> derivatives;  //!< empty for modes without differential states
};

/** @brief save the current state to a file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to save the state to
*/
void saveState(GridDynSimulation* gds,
               const std::string& fileName,
               const SolverMode& sMode = cEmptySolverMode,
               bool append = false);

/** @brief save the bus data to a csv file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to save the bus data to
*/
void saveBusData(GridDynSimulation* gds, const std::string& fileName);

/** @brief save the line data to a csv file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to save the line data to
*/
void saveLineData(GridDynSimulation* gds, const std::string& fileName);

/** @brief save the current state to an XML file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to save the state to
*/
void saveStateXML(GridDynSimulation* gds,
                  const std::string& fileName,
                  const SolverMode& sMode = cEmptySolverMode);

/** @brief save the current solver state to a versioned binary file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to save the state to
@param[in] sMode the SolverMode to save the state
@param[in] append  boolean indicating the file should be appended
@details Each call writes one snapshot; appending requires a compatible binary state file.
The file stores solver vectors only, not the full simulation or event state.
*/
void saveStateBinary(GridDynSimulation* gds,
                     const std::string& fileName,
                     const SolverMode& sMode = cEmptySolverMode,
                     bool append = true);

/** @brief load a state vector from a file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadState(GridDynSimulation* gds,
               const std::string& fileName,
               const SolverMode& sMode = cEmptySolverMode);

/** @brief read the last snapshot from a versioned binary state file
@throws FileOperationError for an unsupported, invalid, or truncated file
*/
BinaryStateData readStateBinary(const std::string& fileName);

/** @brief load the last binary solver-state snapshot into an initialized solver
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
@details This copies solver vectors only. It does not change simulation time, component
state, event queues, or the solver's integration history and is not a checkpoint restore.
The solver mode and vector size must match the snapshot.
@throws FileOperationError on file, layout, or solver mismatch
*/
void loadStateBinary(GridDynSimulation* gds,
                     const std::string& fileName,
                     const SolverMode& sMode = cEmptySolverMode);

/** @brief load a state vector from an XML file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadStateXML(GridDynSimulation* gds,
                  const std::string& fileName,
                  const SolverMode& sMode = cEmptySolverMode);

/** @brief capture a Jacobian and a state to a file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
@param[in] sMode the SolverMode to get the state from
*/
void captureJacState(GridDynSimulation* gds,
                     const std::string& fileName,
                     const SolverMode& sMode = cEmptySolverMode);

/** @brief capture the Jacobian data to a file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
@param[in] sMode the SolverMode to get the state from
*/
void saveJacobian(GridDynSimulation* gds,
                  const std::string& fileName,
                  const SolverMode& sMode = cEmptySolverMode);

/** @brief save the powerflow results to a file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlow(GridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a cdf formatted file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowCdf(GridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to an XML file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowXML(GridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a formatted Text file
 this creates a text file for human readability not machine readability
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowTXT(GridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a binary file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowBinary(GridDynSimulation* gds, const std::string& fileName);

/** @brief save the powerflow results to a csv file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file for storage
*/
void savePowerFlowCSV(GridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from a file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlow(GridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from an XML file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowXML(GridDynSimulation* gds, const std::string& fileName);

/** @brief Save the current power-flow solution in a readable checkpoint file.
 * @details This state-only checkpoint contains the power-flow solver state and bus
 * voltage/angle values. It does not save model parameters, simulation time, events,
 * or time-dependent component history. The power flow must already be initialized.
 */
void savePowerFlowCheckpoint(GridDynSimulation* gds, const std::string& fileName);

/** @brief Restore a power-flow solution into an already initialized compatible model.
 * @details Parameters, simulation time, events, and time-dependent component history
 * are left unchanged. This is not a dynamic or full-simulation checkpoint.
 */
void loadPowerFlowCheckpoint(GridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from an CDF file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowCdf(GridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from a binary file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowBinary(GridDynSimulation* gds, const std::string& fileName);

/** @brief load the powerflow results from a csv file
@param[in] gds  the GridDynSimulation object to operate from
@param[in] fileName the name of the file to load
*/
void loadPowerFlowCSV(GridDynSimulation* gds, const std::string& fileName);

#define STATE_INFORMATION (0)
#define DERIVATIVE_INFORMATION (1)
#define RESIDUAL_INFORMATION (2)
/** @brief write a diagnostic vector in the versioned binary state format

@param[in] time the time associated with the data
@param[in] index an indexing value associated with the data
@param[in] code a code describing the type of information
0 -state information
1 -derivative information
2 -residual information
@param[in] mode the solver mode and offset index associated with the record
@param[in] numElements the number of elements
@param[in] data the data to write to the file
@param[in] fileName the name of the file
@param[in] append indicator if the file should be appended or overwritten(def true)
@throw FileOperationError if the file cannot be opened
*/

void writeVector(CoreTime time,
                 std::uint32_t code,
                 std::uint32_t index,
                 const SolverMode& mode,
                 std::uint32_t numElements,
                 const double* data,
                 const std::string& fileName,
                 bool append = true);

/** @brief write a diagnostic sparse matrix in the versioned binary state format
@param[in] time the time associated with the data
@param[in] code a code describing the type of information in the array
@param[in] index an indexing value associated with the data
@param[in] mode the solver mode and offset index associated with the record
@param[in] matrixValues the Jacobian data to write to the file
@param[in] fileName the name of the file
@param[in] append indicator if the file should be appended or overwritten(def true)
@throw FileOperationError if the file cannot be opened
*/

#define JACOBIAN_DATA 1
void writeArray(CoreTime time,
                std::uint32_t code,
                std::uint32_t index,
                const SolverMode& mode,
                MatrixData<double>& matrixValues,
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
