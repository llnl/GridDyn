/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "DynamicGenerator.h"
#include <string>

namespace griddyn {
class gridBus;
class Block;

/** @brief class defining some additional components for a variable generator such as a renewable
  source like wind and solar the generator includes the addition of a source and a filter block to
  define some sort of input and a filtering function on that input*/
class variableGenerator: public DynamicGenerator {
  protected:
    Source* m_source = nullptr;  //!< reference to the generation source block
    Block* m_cBlock = nullptr;  //!< reference to the control block
    model_parameter mp_Vcutout = -1.0;  //!<[pu] the cutout voltage
    model_parameter mp_Vmax = kBigNum;  //!< [pu] the maximum operating voltage
  public:
    //!< @brief new submodel locations for the extra variable generator block extends the ones
    //!< defined in Generator
    enum extra_block_locations { source_loc = 5, control_block_loc = 6 };
    /** @brief default constructor*/
    explicit variableGenerator(const std::string& objName = "varGen_$");
    variableGenerator(dynModel_t dynModel, const std::string& objName = "varGen_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;

  protected:
    // virtual void pFlowObjectInitializeA (coreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    virtual void add(coreObject* obj) override;

    virtual void add(gridSubModel* obj) override;

    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;

    virtual double getAdjustableCapacityUp(coreTime /*time*/ = maxTime) const override
    {
        return 0.0;
    }  // get the available adjustment Up within the specified timeframe
    virtual double getAdjustableCapacityDown(coreTime /*time*/ = maxTime) const override
    {
        return 0.0;
    }  // get the available adjustment Up within the specified timeframe
    virtual void generationAdjust(double /*adjustment*/) override {}
    virtual coreObject* find(const std::string& object) const override;
    virtual coreObject* getSubObject(const std::string& typeName, index_t num) const override;

  protected:
    virtual double pSetControlUpdate(const IOdata& inputs,
                                     const stateData& sD,
                                     const solverMode& sMode) override;
    virtual index_t pSetLocation(const solverMode& sMode) override;
};

}  // namespace griddyn
