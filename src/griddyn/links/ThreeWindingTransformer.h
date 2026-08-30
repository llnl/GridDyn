/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../primary/AcBus.h"
#include "AcLine.h"
#include "Subsystem.h"
#include <array>
#include <queue>
#include <string>

namespace griddyn::links {
/** @brief class defining a thee winding transformer model
 */
class ThreeWindingTransformer: public Subsystem {
  private:
    int faultLink = -1;  //!< link number of the fault if one is present
    AcBus* starBus = nullptr;
    std::array<AcLine*, 3> windingLegs{};
    double segmentationLength = 0.0;
    double length = 0.0;
    double fault = -1.0;

    AcLine* windingLeg(index_t winding) const;

  public:
    /** @brief default constructor*/
    ThreeWindingTransformer(const std::string& objName = "ThreeWinding_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    // add components
    virtual void add(CoreObject* obj) override final;  // there shouldn't be any additional adds
    // remove components
    virtual void remove(CoreObject* obj)
        override final;  // there shouldn't be any removes all models are controlled internally

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    /** Configure one external-winding-to-star leg.  Winding numbers are 1--3. */
    void setWindingImpedance(index_t winding,
                             double resistance,
                             double reactance,
                             units::unit unitType = units::defunit);
    void setWindingTap(index_t winding,
                       double tap,
                       double phaseShift = 0.0,
                       units::unit phaseUnit = units::rad);
    void setWindingRatings(index_t winding,
                           double ratingA,
                           double ratingB = 0.0,
                           double ratingC = 0.0,
                           units::unit unitType = units::MW);
    void setWindingStatus(index_t winding, bool enabled);
    /** Apply PSS/E magnetizing data to the first winding, the standard star equivalent. */
    void setMagnetizing(double conductance,
                        double susceptance,
                        units::unit unitType = units::defunit);
    void setStarVoltageAngle(double voltage, double angle, units::unit angleUnit = units::rad);
    void followNetwork(int network, std::queue<GridBus*>& stk) override;
    void updateBus(GridBus* bus, index_t busnumber) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
};

}  // namespace griddyn::links
