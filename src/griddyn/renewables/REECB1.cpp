/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REECB1.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <string>

namespace griddyn {

REECB1::REECB1(const std::string& name):REECA1(name)
{
    set("imax",999.0);
}

CoreObject* REECB1::clone(CoreObject* obj) const
{
    return cloneBase<REECB1,REECA1>(this,obj);
}

void REECB1::set(std::string_view param,double val,units::unit unitType)
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key=="pflag" || key=="vref1" ||
        (key.size()==3 && key[2]>='1' && key[2]<='4' &&
         (key.starts_with("ip") || key.starts_with("iq") ||
          key.starts_with("vp") || key.starts_with("vq")))) {
        throw InvalidParameterValue("REECB1 does not use REECA1 speed or V-I curve parameters");
    }
    REECA1::set(param,val,unitType);
    if (key=="imax") {
        for (int index=1;index<=4;++index) {
            const auto suffix=std::to_string(index);
            REECA1::set("ip"+suffix,val);
            REECA1::set("iq"+suffix,val);
        }
    } else if (key=="tp") {
        REECA1::set("tpfilt",val);
    }
}

} // namespace griddyn
