/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn::loads {
/**
 * @brief Compose heterogeneous load models at a common electrical terminal.
 *
 * Child loads participate in the normal GridDyn state, residual, root, and Jacobian
 * aggregation. The parent P/Q is divided among children using one-based
 * `fractionN`/`componentfractionN` settings. Fractions not explicitly set share the
 * remaining fraction equally; fully specified fractions must sum to one. ZIP child current
 * and admittance terms are scaled by the same fraction.
 *
 * This is the component-composition layer for future composite distribution-load models.
 * All child loads currently see the parent load's bus voltage and frequency. A model that
 * includes the WECC internal transformer and feeder network must add those network nodes and
 * links as a separate layer.
 */
class CompositeLoad: public GridLoad {
  public:
    explicit CompositeLoad(const std::string& objName = "loadComposition_$");

    CoreObject* clone(CoreObject* obj = nullptr) const override;

    void add(CoreObject* obj) override;
    void remove(CoreObject* obj) override;

    void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param,
             double val,
             units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;

    count_t componentCount() const;
    GridLoad* component(index_t index) const;
    void setComponentReactiveBase(index_t index, double reactivePower);

    double getRealPower(const IOdata& inputs,
                        const StateData& stateData,
                        const SolverMode& sMode) const override;
    double getReactivePower(const IOdata& inputs,
                            const StateData& stateData,
                            const SolverMode& sMode) const override;
    double getRealPower(double voltage) const override;
    double getReactivePower(double voltage) const override;
    double getRealPower() const override;
    double getReactivePower() const override;

    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
    void ioPartialDerivatives(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode) override;
    count_t outputDependencyCount(index_t outputNum, const SolverMode& sMode) const override;

  protected:
    void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

  private:
    struct ZipReferenceTerms {
        double ip = 0.0;
        double iq = 0.0;
        double yp = 0.0;
        double yq = 0.0;
        bool captured = false;
    };

    std::vector<double> componentFractions;
    std::vector<std::optional<double>> componentReactiveBases;
    std::vector<ZipReferenceTerms> zipReferenceTerms;

    std::vector<GridLoad*> getComponents() const;
    std::vector<double> resolveFractions() const;
    void allocateComponentPowers();
    void setComponentFraction(index_t index, double fraction);
};
}  // namespace griddyn::loads
