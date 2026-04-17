#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zipLoad.h"
#include <string>
namespace griddyn {
/** contains the different types of loads that can be used in griddyn*/
namespace loads {
    /** @brief a load with ramping of the load types*/
    class rampLoad: public zipLoad {
      protected:
        double dPdt = 0.0;  //!< [pu] real component of the load (constant Power)
        double dQdt = 0.0;  //!< [pu] imaginary component of the load (constant Power)
        double drdt = 0.0;  //!< [pu] resistive load (constant impedance)
        double dxdt = 0.0;  //!< [pu] reactive load (constant impedance)
        double dIpdt = 0.0;  //!< [pu] real current; (constant current)
        double dIqdt = 0.0;  //!< [pu] imaginary current (constant current)
        double dYpdt = 0.0;  //!< [pu] ramp in real impedance power
        double dYqdt = 0.0;  //!< [pu] ramp in imaginary constant impedance power
      public:
        explicit rampLoad(const std::string& objName = "rampLoad_$");
        rampLoad(double rP, double qP, const std::string& objName = "rampLoad_$");

        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;
        /** set the ramps to 0*/
        void clearRamp();
    };
}  // namespace loads
}  // namespace griddyn
