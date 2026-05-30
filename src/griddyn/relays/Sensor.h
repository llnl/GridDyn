/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class GridBlock;
class GrabberSet;
/** @brief class implementing a sensor relay object
 a sensor can contain a set of basic control blocks and data grabbers which can grab data from any
other object in the system and run in through a set of processes to obtain a result the result can
be output or used as a basis for other relay actions
*/
class Sensor: public Relay {
  public:
    /** @brief sensor flags controlling operation
     */
    enum SensorFlags {
        DIRECT_IO = object_flag6,  //!< indication that the sensor is directly listing all inputs as
                                   //!< outputs with
        //!< no processing
        LINK_TYPE_SOURCE = object_flag7,  //!< indication that the source is a link
        LINK_TYPE_SINK = object_flag8,  //!< indicator that the sink is a link object
        NO_MESSAGE_REPLY =
            object_flag9,  //!< indicator that the sensor should not send message replies
        FORCE_CONTINUOUS =
            object_flag10,  //!< force continuous operation even if the underlying data sources are
        //!< not continuously available
    };
    /** @brief define the possible operation modes for a processing sequence*/
    enum class SequenceMode : unsigned char {
        NORMAL,  //!< the sequence runs under normal operation with both state and sampled input
                 //!< available
        SAMPLED,  //!< the sequence only runs with periodic sampling
        DISABLED,  //!< the sequence is disabled
    };
    /** @brief enumeration of the output modes available for the outputs
     the output can be direct from an in input grabber, it can be directly from a block processing
    object or it can a function of block or inputs*/
    enum class OutputMode : unsigned char {
        BLOCK,  //!< direct from a filter block output
        BLOCK_DERIV,  //!< direct time derivative of a block
        PROCESSED,  //!< processed output
        DIRECT,  //!< direct from an input
    };

  protected:
    std::vector<int> blockInputs;  //!< the number of the input Source
    // PT leaving outputs as int (vs index_t) as negative values here are meaningful and useful
    std::vector<int> outputs;  //!< locations of output values
    std::vector<stringVec> outputStrings;  //!< names for the outputs
    std::vector<OutputMode> outputMode;  //!< output modes corresponding to the outputs
    std::vector<SequenceMode> processStatus;  //!< the status of the processing sequence
    stringVec inputStrings;  //!< vector of input strings
    index_t m_terminal = 0;  //!< the terminal to use on link operations  NOTE: works with
                             //!< link_source and link_sink flags
    count_t instructionCounter = 0;  //!< the number of instructions the relay has received
    std::vector<std::shared_ptr<GrabberSet>> dataSources;  // the data sources for the output
    std::vector<GridBlock*> filterBlocks;  //!< the filtered blocks
    std::vector<std::shared_ptr<GrabberSet>> outGrabbers;  //!< Grabbers for the output;
    std::vector<std::shared_ptr<GridBlock>>
        blkptrs;  //!< storage locations for the shared_ptr of blocks
  public:
    /** @brief default constructor*/
    explicit Sensor(const std::string& objName = "sensor_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    using Relay::add;
    virtual void add(CoreObject* obj) override;
    /** @brief add a filter block to the relay
    @param[in] blk a pointer to a filter block
    */
    virtual void add(GridBlock* blk);
    /** @brief add a shared pointer to a GrabberSet
    @param[in] dGr a shared pointer to GrabberSet Object
    */
    virtual void add(std::shared_ptr<GrabberSet> grabberSet);

    /** @brief add a shared pointer to a GridGrabber object
    @param[in] dGr a shared pointer to GrabberSet Object
    */
    virtual void add(std::shared_ptr<GridGrabber> gridGrabber);

    /** retrieve the GrabberSet based on index
    @return a shared_ptr to a grabberset object that is used in the data retrieval*/
    std::shared_ptr<GrabberSet> getGrabberSet(index_t grabberNum);

    // dynamic functions for evaluation with a limit exceeded
    virtual void timestep(coreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  matrixData<double>& matrixDataValue,
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

    virtual double getOutput(const IOdata& inputs,
                             const StateData& stateDataValue,
                             const SolverMode& sMode,
                             index_t outNum = 0) const override;
    virtual double getOutput(index_t outNum = 0) const override;
    virtual index_t getOutputLoc(const SolverMode& sMode, index_t outNum) const override;
    virtual IOdata getOutputs(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode) const override;

    /** @brief get the block output from the sensor
    @param[in] sD  the state data to get the output from
    @param[in] sMode  the SolverMode corresponding to the data
    @param[in] blockNumber the number of the block to get the output from
    @return a double with the requested block output
    */
    double getBlockOutput(const StateData& stateDataValue,
                          const SolverMode& sMode,
                          index_t blockNumber) const;

    /** @brief get the block rate of change from the sensor
    @param[in] sD  the state data to get the output from
    @param[in] sMode  the SolverMode corresponding to the data
    @param[in] blockNumber the number of the block to get the output from
    @return a double with the requested block output rate of change
    */
    double getBlockDerivOutput(const StateData& stateDataValue,
                               const SolverMode& sMode,
                               index_t blockNumber) const;

    /** @brief get the raw sensor input
    @param[in] sD  the state data to get the output from
    @param[in] sMode  the SolverMode corresponding to the data
    @param[in] inputNumber the input of the index to get the value
    @return a double with the requested raw input
    */
    double getInput(const StateData& stateDataValue,
                    const SolverMode& sMode,
                    index_t inputNumber = 0) const;
    virtual void updateA(coreTime time) override;
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          const SolverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

    virtual void receiveMessage(std::uint64_t sourceID,
                                std::shared_ptr<CommMessage> message) override;

    virtual void updateObject(CoreObject* obj,
                              ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override;
    virtual void getObjects(std::vector<CoreObject*>& objects) const override;

    virtual const std::vector<stringVec>& outputNames() const override;

  protected:
    /** define an output based on a string*/
    void setupOutput(index_t num, const std::string& outputString);

  private:
    /** @brief generate the input grabbers
    used in the initialize function
    */
    void generateInputGrabbers();
    /** get the input to a particular block based on inputs and StateData*/
    double getBlockInput(index_t blockNum,
                         const IOdata& inputs,
                         const StateData& stateDataValue,
                         const SolverMode& sMode) const;
    /** get the input to a block based on inputs only*/
    double getBlockInput(index_t blockNum, const IOdata& inputs) const;
};
}  // namespace griddyn
