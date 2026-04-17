#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Load.h"
#include <string>
namespace griddyn::loads {
    /** primary load class supports 3 main types of loads  constant power, constant impedance,
constant current these loads should for the basis of most non dynamic load models following the ZIP
model Z-constant impedance, I-constant current, P- constant Power
*/
    class frequencySensitiveLoad: public Load {
      private:
        double dPdf = 0.0;  //!< factor for determining how sensitive Pout is to frequency
        double Pout = 0.0;  //!< Pout before applying frequency variation
        double Qout = 0.0;  //!< Qout before applying frequency variation
      protected:
        model_parameter M = 0.0;  //!< load droop factor
        model_parameter H = 0.0;  //!< load inertia used in computing dPdf
        Load* subLoad;  //!< pointer to the subload type
      public:
        explicit frequencySensitiveLoad(const std::string& objName = "load_$");

        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
        virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual void setFlag(const std::string& flag, bool val = true) override;

        virtual double get(const std::string& param,
                           units::unit unitType = units::defunit) const override;

        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;
        /** update the actual outputs with a frequency related calculation*/
        virtual void updateOutputs(double frequency);
        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;

        virtual void ioPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode) override;
        virtual void outputPartialDerivatives(const IOdata& inputs,
                                              const stateData& sD,
                                              matrixData<double>& md,
                                              const solverMode& sMode) override;
        virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;

        virtual double getRealPower(const IOdata& inputs,
                                    const stateData& sD,
                                    const solverMode& sMode) const override;
        virtual double getReactivePower(const IOdata& inputs,
                                        const stateData& sD,
                                        const solverMode& sMode) const override;
        virtual double getRealPower(double V) const override;
        virtual double getReactivePower(double V) const override;
        virtual double getRealPower() const override;
        virtual double getReactivePower() const override;  // for saving the state
    };

}  // namespace griddyn::loads
