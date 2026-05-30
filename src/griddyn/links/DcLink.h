/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Link.h"
#include <string>

namespace griddyn::links {
/** implementing a DC transmission line model
 */
class DcLink: public Link {
  public:
    /*  enum DcLinkFlags
  {
    fixedTargetPower = object_flag5,
  };*/
  protected:
    double Idc = 0;  //!< [puA] storage for DC current
    double r = 0;  //!< [puOhm]  the dc resistance
    double x = 0.0001;  //!< [puOhm]  the dc inductance
  public:
    DcLink(const std::string& objName = "dclink_$");
    DcLink(double resistancePu, double reactancePu, const std::string& objName = "dclink_$");
    // Link(double max_power,GridBus *bus1, GridBus *bus2);

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void updateBus(GridBus* bus, index_t busnumber) override;

    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& stateData,
                                  const SolverMode& sMode) override;

    virtual double getMaxTransfer() const override;
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual StateSizes localStateSizes(const SolverMode& sMode) const override;
    virtual count_t localJacobianCount(const SolverMode& sMode) const override;

    virtual void timestep(coreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual double quickupdateP() override { return 0; }

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    // dynInitializeB dynamics
    // virtual void dynObjectInitializeA (coreTime time0, std::uint32_t flags);
    using Link::ioPartialDerivatives;
    virtual void ioPartialDerivatives(id_type_t busId,
                                      const StateData& stateData,
                                      matrixData<double>& jacobian,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    using Link::outputPartialDerivatives;
    virtual void outputPartialDerivatives(id_type_t busId,
                                          const StateData& stateData,
                                          matrixData<double>& jacobian,
                                          const SolverMode& sMode) override;

    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  matrixData<double>& jacobian,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void residual(const IOdata& inputs,
                          const StateData& stateData,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;
    virtual void guessState(coreTime time,
                            double state[],
                            double dstateDt[],
                            const SolverMode& sMode) override;
    // for computing all the Jacobian elements at once
    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
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

}  // namespace griddyn::links
