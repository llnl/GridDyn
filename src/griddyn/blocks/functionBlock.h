/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include "utilities/functionInterpreter.h"
#include <string>

namespace griddyn::blocks {
/** @brief class implementing a function operation on the input
a wide assortment of functions are available including trig, logs, and other common math
operations*/
class functionBlock: public Block {
  public:
    //!< flags for function block
    enum functionblock_flags {
        uses_constantarg =
            object_flag10,  //!< flag indicating that the function should use a constant
                            //!< argument for the second argument of functions
    };

  protected:
    function1_t fptr = nullptr;  //!< function object for single argument functions
    function2_t fptr2 = nullptr;  //!< function object for multiple argument functions
    model_parameter gain = 1.0;  //!< extra gain factor
    model_parameter arg2 = 0.0;  //!< second argument for 2 argument functions
  public:
    /** @brief default constructor*/
    functionBlock();
    /** @brief alternate constructor
@param[in] functionName the name of the function as a string
*/
    explicit functionBlock(const std::string& functionName);
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // virtual index_t findIndex(const std::string &field, const solverMode &sMode) const;

    // virtual void blockDerivative(double input, double didt, const stateData &sD, double
    // deriv[], const solverMode &sMode) override;
    virtual void blockAlgebraicUpdate(double input,
                                      const stateData& sD,
                                      double update[],
                                      const solverMode& sMode) override;

    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const stateData& sD,
                                       matrixData<double>& md,
                                       index_t argLoc,
                                       const solverMode& sMode) override;
    virtual double step(coreTime time, double input) override;
    // virtual void setTime(coreTime time){prevTime=time;};
  protected:
    /** @brief load the function objects from a string input
@param[in] functionName the name of the function as a string*/
    void setFunction(const std::string& functionName);
};
}  // namespace griddyn::blocks
