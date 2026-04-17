/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_GENMODELS_GENMODELCLASSICAL_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_GENMODELS_GENMODELCLASSICAL_H_

#include "../GenModel.h"
#include <string>

namespace griddyn {
namespace genmodels {
    class GenModelClassical: public GenModel {
      public:
        /** @brief set of flags used by genModels for variations in computation
         */

      protected:
        double H = 5.0;  //!< [pu] inertial constant
        double D = 0.04;  //!< [pu] damping
        double Vd = 0;  //!< the computed d axis voltage
        double Vq = 0;  //!< the computed q axis voltage
        double mp_Kw = 13.0;  //!< speed gain for the damping system
        count_t seqId = 0;  //!< the sequence Id the voltages were computed for
      public:
        //!< @brief default constructor
        explicit GenModelClassical(const std::string& objName = "genModelClassic_#");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual stringVec localStateNames() const override;
        // dynamics

        virtual void residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode) override;
        virtual void derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode) override;
        virtual IOdata getOutputs(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) const override;

        using GenModel::getOutput;
        virtual double getOutput(const IOdata& inputs,
                                 const stateData& sD,
                                 const solverMode& sMode,
                                 index_t numOut = 0) const override;

        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;
        virtual void outputPartialDerivatives(const IOdata& inputs,
                                              const stateData& sD,
                                              matrixData<double>& md,
                                              const solverMode& sMode) override;

        virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;
        virtual void ioPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const IOlocs& inputLocs,
                                          const solverMode& sMode) override;

        virtual void algebraicUpdate(const IOdata& inputs,
                                     const stateData& sD,
                                     double update[],
                                     const solverMode& sMode,
                                     double alpha) override;
        /** helper function to get omega and its state location
         */
        virtual double getFreq(const stateData& sD,
                               const solverMode& sMode,
                               index_t* freqOffset = nullptr) const override;
        virtual double getAngle(const stateData& sD,
                                const solverMode& sMode,
                                index_t* angleOffset = nullptr) const override;
        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;

      protected:
        void computeInitialAngleAndCurrent(const IOdata& inputs,
                                           const IOdata& desiredOutput,
                                           double R1,
                                           double X1);
    };

}  // namespace genmodels
}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_GENMODELS_GENMODELCLASSICAL_H_
