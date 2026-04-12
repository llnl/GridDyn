/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRAMODELS_TXLIFESPAN_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRAMODELS_TXLIFESPAN_H_

#include "griddyn/relays/sensor.h"
#include <string>

namespace griddyn {
namespace extra {
    /** @brief class modeling a transformer lifespan based on thermal effects
     */
    class txLifeSpan: public sensor {
      public:
        enum lifespan_model_flags {
            useIECmethod = object_flag11,
            no_disconnect =
                object_flag12,  //!< flag indicating that the object should create a short circuit
                                //!< instead of disconnecting when life reaches 0
        };

      protected:
        double initialLife = 150000.0;  //!< initial life in hours
        double agingConstant = 14594.0;  //!< aging constant default value of 14594 based on
                                         //!< research 15000 is another commonly used value
        double baseTemp = 110;  //!< the temperature base for the lifespan equations
        double agingFactor =
            1.0;  //!<  factor for accelerated or decelerated aging based on insulation properties

      private:
        double Faa = 0.0;

      public:
        txLifeSpan(const std::string& objName = "txlifeSpan_$");
        coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void setFlag(const std::string& flag, bool val = true) override;
        virtual void set(const std::string& param, const std::string& val) override;

        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        using sensor::add;
        virtual void add(coreObject* obj) override final;
        virtual double get(const std::string& param,
                           units::unit unitType = units::defunit) const override;

        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
        virtual void updateA(coreTime time) override;

        void actionTaken(index_t ActionNum,
                         index_t conditionNum,
                         change_code actionReturn,
                         coreTime /*actionTime*/) override;
    };

}  // namespace extra
}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRAMODELS_TXLIFESPAN_H_
