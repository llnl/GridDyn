/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "gridComponent.h"
#include <string>

namespace griddyn {
/** @brief base class for any model can act as a component of another model
* gridSubModel class defines the interface for models which can act as components of other models
such as Exciter, or Governor most of the differential equations are contained in submodels.  The
interface is meant to be flexible so unlike gridSecondary models there is no predefined interface,
but at the same time many of the function calls are intended to be the same,  The main difference
being there is only one initialize function, they can operate in power flow but those objects just
call initialize twice

**/
class gridSubModel: public gridComponent {
  protected:
    double m_output = 0.0;  //!< storage location for the current output
  public:
    /** @brief default constructor*/
    explicit gridSubModel(const std::string& objName = "submodel_#");

    virtual void pFlowInitializeA(coreTime time, std::uint32_t flags) override final;

    virtual void pFlowInitializeB() override final;

    virtual void dynInitializeA(coreTime time, std::uint32_t flags) override final;

    virtual void dynInitializeB(const IOdata& inputs,
                                const IOdata& desiredOutput,
                                IOdata& fieldSet) override final;

    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;
};

}  // namespace griddyn
