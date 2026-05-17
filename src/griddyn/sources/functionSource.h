/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Source.h"
#include <functional>
#include <string>

namespace griddyn::sources {
/** source allowing the specification of an arbitrary function as the source generator
@details uses a function that is dependent on time the function should not have state as the input
time is not necessarily unidirectional
*/
class functionSource: public Source {
  private:
    std::function<double(double)> sourceFunc;  //!< the function object used as a signal generator

  public:
    functionSource(const std::string& objName = "functionsource_#");

    CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& sD,
                              const solverMode& sMode) const override;
    virtual double getOutput(const IOdata& inputs,
                             const stateData& sD,
                             const solverMode& sMode,
                             index_t outputNum = 0) const override;

    virtual double getOutput(index_t outputNum = 0) const override;

    virtual double getDoutdt(const IOdata& inputs,
                             const stateData& sD,
                             const solverMode& sMode,
                             index_t outputNum = 0) const override;
    /** set the generation function
@details the function should not have state as the input time is not unidirectional
@param[in] calcFunc function that takes time as a double argument and produces a double as an
output which is used as the output of the source
*/
    void setFunction(std::function<double(double)> calcFunc);
};
}  // namespace griddyn::sources

