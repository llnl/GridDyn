/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GridComponent.h"
#include <string>

namespace griddyn {
/** @brief base class for any model can act as a component of another model
* GridSubModel class defines the interface for models which can act as components of other models
such as Exciter, or Governor most of the differential equations are contained in submodels.  The
interface is meant to be flexible so unlike GridSecondary models there is no predefined interface,
but at the same time many of the function calls are intended to be the same,  The main difference
being there is only one initialize function, they can operate in power flow but those objects just
call initialize twice

**/
class GridSubModel: public GridComponent {
  protected:
    double m_output = 0.0;  //!< storage location for the current output
  public:
    /** @brief default constructor*/
    explicit GridSubModel(const std::string& objName = "submodel_#");

    virtual void pFlowInitializeA(CoreTime time, std::uint32_t flags) override final;

    virtual void pFlowInitializeB() override final;

    virtual void dynInitializeA(CoreTime time, std::uint32_t flags) override final;

    virtual void dynInitializeB(const IOdata& inputs,
                                const IOdata& desiredOutput,
                                IOdata& fieldSet) override final;

    /** Supply an initialization target for one output of a shared submodel.
     *
     * Most submodels do not need this hook and return false. Multi-output
     * controllers such as IEEEG1 use it to collect the initialized mechanical
     * powers of generators which consume outputs owned by another generator.
     */
    virtual bool setOutputInitializationTarget(index_t outputIndex, double target);

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
};

}  // namespace griddyn
