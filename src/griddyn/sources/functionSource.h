/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_FUNCTIONSOURCE_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_FUNCTIONSOURCE_H_

#include "../Source.h"
#include <functional>
#include <string>

namespace griddyn {
namespace sources {
    /** source allowing the specification of an arbitrary function as the source generator
@details uses a function that is dependent on time the function should not have state as the input
time is not necessarily unidirectional
*/
    class functionSource: public Source {
      private:
        std::function<double(double)>
            sourceFunc;  //!< the function object used as a signal generator

      public:
        functionSource(const std::string& objName = "functionsource_#");

        coreObject* clone(coreObject* obj = nullptr) const override;

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
    @param[in] a function that takes a time as a double as an argument and produces and double as an
    output which is used as the output of the source
    */
        void setFunction(std::function<double(double)> calcFunc);
    };
}  // namespace sources
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_FUNCTIONSOURCE_H_
