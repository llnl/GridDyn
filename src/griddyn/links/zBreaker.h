/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Link.h"
#include <string>

namespace griddyn::links {
/** @brief class that acts as a zero impedance tie
used for implementing a bus-breaker model of a power system as well as creating slave buses and a
few other types of linkages
*/
class zBreaker: public Link {
  protected:
    bool& merged;  //!< flag indicating that the buses have been merged using the extra bool in
                   //!< coreObject
  public:
    zBreaker(const std::string& objName = "zbreaker_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    // parameter set functions

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void switchMode(index_t num, bool mode) override;

    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) override;
    virtual double quickupdateP() override;

    virtual int fixRealPower(double power,
                             id_type_t measureTerminal,
                             id_type_t fixedTerminal = 0,
                             units::unit = units::defunit) override;

    virtual int fixPower(double rPower,
                         double qPower,
                         id_type_t measureTerminal,
                         id_type_t fixedTerminal = 0,
                         units::unit = units::defunit) override;

    virtual void coordinateMergeStatus();

  protected:
    virtual void switchChange(int switchNum) override;
    /** @brief merge the two buses together*/
    void merge();
    /** @brief unmerge the merged buses*/
    void unmerge();
};

}  // namespace griddyn::links
