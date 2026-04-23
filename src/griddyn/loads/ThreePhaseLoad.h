/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include <string>
#include <vector>
namespace griddyn {
class gridBus;
namespace loads {
    enum class phase_type_t {
        abc,
        pnz,
    };
    /** Three phase load is a base object for supporting 3-phase constant power loads includes a few
conversions to positive sequence values.
*/
    class ThreePhaseLoad: public Load {
      public:
        enum threephase_load_flags {
            use_abs_angle = object_flag5,
            three_phase_output = object_flag6,
            three_phase_input = object_flag7,
        };

      private:
        double Pa = 0.0;  //!<[pu] A Phase real power
        double Pb = 0.0;  //!<[pu] B Phase real power
        double Pc = 0.0;  //!<[pu] C Phase real power
        double Qa = 0.0;  //!<[pu] A Phase reactive power
        double Qb = 0.0;  //!<[pu] B Phase reactive power
        double Qc = 0.0;  //!<[pu] C Phase reactive power
        double multiplier = 1.0;  //!< phase multiplier for amplifying current inputs
      public:
        explicit ThreePhaseLoad(const std::string& objName = "load_$");
        ThreePhaseLoad(double rP, double rQ, const std::string& objName = "load_$");

        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void set(std::string_view param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual void setFlag(std::string_view flag, bool val = true) override;

        virtual double get(std::string_view param,
                           units::unit unitType = units::defunit) const override;

        /** set the real output power with the specified units
    @param[in] level the real power output setting
    @param[in] unitType the units on the real power
    */
        virtual void setLoad(double level, units::unit unitType = units::defunit) override;
        /** set the real and reactive output power with the specified units
    @param[in] Plevel the real power output setting
    @param[in] Qlevel the reactive power output setting
    @param[in] unitType the units on the real power
    */
        virtual void
            setLoad(double Plevel, double Qlevel, units::unit unitType = units::defunit) override;
        // for saving the state
        virtual IOdata getRealPower3Phase(const IOdata& inputs,
                                          const stateData& sD,
                                          const solverMode& sMode,
                                          phase_type_t type = phase_type_t::abc) const;
        virtual IOdata getReactivePower3Phase(const IOdata& inputs,
                                              const stateData& sD,
                                              const solverMode& sMode,
                                              phase_type_t type = phase_type_t::abc) const;
        /** get the 3 phase real output power that based on the given voltage
    @param[in] V the bus voltage
    @return the real power consumed by the load*/
        virtual IOdata getRealPower3Phase(const IOdata& V,
                                          phase_type_t type = phase_type_t::abc) const;
        /** get the 3 phase reactive output power that based on the given voltage
    @param[in] V the bus voltage
    @return the reactive power consumed by the load*/
        virtual IOdata getReactivePower3Phase(const IOdata& V,
                                              phase_type_t type = phase_type_t::abc) const;
        virtual IOdata getRealPower3Phase(phase_type_t type = phase_type_t::abc) const;
        virtual IOdata getReactivePower3Phase(phase_type_t type = phase_type_t::abc) const;

        void setPa(double val);
        void setPb(double val);
        void setPc(double val);
        void setQa(double val);
        void setQb(double val);
        void setQc(double val);

        virtual const std::vector<stringVec>& inputNames() const override;
        virtual const std::vector<stringVec>& outputNames() const override;

      private:
        double getBaseAngle() const;
    };
}  // namespace loads
}  // namespace griddyn
