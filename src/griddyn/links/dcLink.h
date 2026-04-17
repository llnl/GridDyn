/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_DCLINK_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_DCLINK_H_

#include "../Link.h"
#include <string>

namespace griddyn {
namespace links {
    /** implementing a DC transmission line model
     */
    class dcLink: public Link {
      public:
        /*  enum dclink_flags
      {
        fixed_target_power = object_flag5,
      };*/
      protected:
        double Idc = 0;  //!< [puA] storage for DC current
        double r = 0;  //!< [puOhm]  the dc resistance
        double x = 0.0001;  //!< [puOhm]  the dc inductance
      public:
        dcLink(const std::string& objName = "dclink_$");
        dcLink(double rP, double Lp, const std::string& objName = "dclink_$");
        // Link(double max_power,gridBus *bus1, gridBus *bus2);

        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void updateBus(gridBus* bus, index_t busnumber) override;

        virtual void updateLocalCache() override;
        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;

        virtual double getMaxTransfer() const override;
        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void pFlowObjectInitializeB() override;

        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual stateSizes LocalStateSizes(const solverMode& sMode) const override;
        virtual count_t LocalJacobianCount(const solverMode& sMode) const override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

        virtual double quickupdateP() override { return 0; }

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        // dynInitializeB dynamics
        // virtual void dynObjectInitializeA (coreTime time0, std::uint32_t flags);
        using Link::ioPartialDerivatives;
        virtual void ioPartialDerivatives(id_type_t busId,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode) override;
        using Link::outputPartialDerivatives;
        virtual void outputPartialDerivatives(id_type_t busId,
                                              const stateData& sD,
                                              matrixData<double>& md,
                                              const solverMode& sMode) override;

        virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;
        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
        virtual void residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode) override;
        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;
        virtual void guessState(coreTime time,
                                double state[],
                                double dstate_dt[],
                                const solverMode& sMode) override;
        // for computing all the Jacobian elements at once
        virtual void getStateName(stringVec& stNames,
                                  const solverMode& sMode,
                                  const std::string& prefix = "") const override;
        virtual int fixRealPower(double power,
                                 id_type_t measureTerminal,
                                 id_type_t fixedTerminal = 0,
                                 units::unit unitType = units::defunit) override;
        virtual int fixPower(double power,
                             double qPower,
                             id_type_t measureTerminal,
                             id_type_t fixedTerminal = 0,
                             units::unit unitType = units::defunit) override final;
    };

}  // namespace links
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_DCLINK_H_
