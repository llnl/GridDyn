/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "aggregateLoad.h"

#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include <cmath>
#include <string>
namespace griddyn::loads {
static typeFactory<aggregateLoad> glfld("load", stringVec{"composite", "cluster", "group"});

using gmlc::utilities::stringOps::splitline;
using gmlc::utilities::stringOps::trailingStringInt;
using gmlc::utilities::stringOps::trim;
aggregateLoad::aggregateLoad(const std::string& objName): zipLoad(objName)
{
    aggregateLoad::add(new zipLoad(getName() + "sub"));
}

coreObject* aggregateLoad::clone(coreObject* obj) const
{
    auto nobj = cloneBase<aggregateLoad, zipLoad>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->consumeSimpleLoad = consumeSimpleLoad;
    nobj->fraction = fraction;

    for (auto& ld : subLoads) {
        nobj->add(ld->clone(nullptr));
    }

    return nobj;
}

void aggregateLoad::add(zipLoad* ld)
{
    if (ld->locIndex != kNullLocation) {
        if (static_cast<index_t>(subLoads.size()) <= ld->locIndex) {
            subLoads.resize(ld->locIndex + 1, nullptr);
            fraction.resize(ld->locIndex + 1, -1);
        }
        subLoads[ld->locIndex] = ld;
        addSubObject(ld);
    } else {
        subLoads.push_back(ld);
        fraction.push_back(-1.0);
        ld->locIndex = static_cast<index_t>(subLoads.size()) - 1;
        addSubObject(ld);
    }
}

void aggregateLoad::add(coreObject* obj)
{
    auto* ld = dynamic_cast<zipLoad*>(obj);
    if (ld != nullptr) {
        return add(ld);
    }
    throw(unrecognizedObjectException(this));
}

void aggregateLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    // TODO(phlpt): Need to rethink this object.
    zipLoad::pFlowInitializeA(time0, flags);
    if (consumeSimpleLoad) {
        int nLoads = bus->getInt("loadcount");

        if (nLoads == 0) {
            return;
        }
        zipLoad* sLoad = nullptr;
        zipLoad* testLoad = nullptr;
        double mxP = 0;
        for (int kk = 0; kk < nLoads; ++kk) {
            testLoad = static_cast<zipLoad*>(getParent()->getSubObject("load", kk));
            if (testLoad->getID() == getID()) {
                continue;
            }
            if (std::abs(testLoad->getRealPower()) > mxP) {
                mxP = std::abs(testLoad->getRealPower());
                sLoad = testLoad;
            }
        }
        if (sLoad == nullptr) {
            return;
        }
        // do a first pass of loading
        double rem = 1.0;

        // we know sLoad is actually an aggregateLoad
        auto aggregateSLoad = static_cast<aggregateLoad*>(sLoad);

        setP(aggregateSLoad->getP());
        setQ(aggregateSLoad->getQ());
        setIp(aggregateSLoad->getIp());
        setIq(aggregateSLoad->getIq());
        setup(aggregateSLoad->getYp());
        setYq(aggregateSLoad->getYq());

        for (size_t nn = 0; nn < subLoads.size(); ++nn) {
            if (fraction[nn] > 0) {
                subLoads[nn]->set("p", getP() * fraction[nn]);
                subLoads[nn]->set("q", getQ() * fraction[nn]);
                subLoads[nn]->set("ip", getIp() * fraction[nn]);
                subLoads[nn]->set("iq", getIq() * fraction[nn]);
                subLoads[nn]->set("yp", getYp() * fraction[nn]);
                subLoads[nn]->set("yq", getYq() * fraction[nn]);
                rem -= fraction[nn];
            }
        }
        double remnegcnt = 0;
        for (auto& sL : fraction) {
            if (sL < 0) {
                remnegcnt += 1.0;
            }
        }
        if (remnegcnt > 0) {
            mxP = rem / remnegcnt;
            for (size_t nn = 0; nn < subLoads.size(); ++nn) {
                if (fraction[nn] < 0) {
                    subLoads[nn]->set("p", getP() * mxP);
                    subLoads[nn]->set("q", getQ() * mxP);
                    subLoads[nn]->set("ip", getIp() * mxP);
                    subLoads[nn]->set("iq", getIq() * mxP);
                    subLoads[nn]->set("yp", getYp() * mxP);

                    subLoads[nn]->set("yq", getYq() * mxP);
                    fraction[nn] = mxP;
                }
            }
        }
        testLoad->disable();
    }

    for (auto& ld : subLoads) {
        ld->pFlowInitializeA(time0, flags);
    }
}

void aggregateLoad::pFlowObjectInitializeB()
{
    for (auto& ld : subLoads) {
        ld->pFlowInitializeB();
    }
}

void aggregateLoad::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    for (auto& ld : subLoads) {
        ld->dynInitializeA(time0, flags);
    }
}

void aggregateLoad::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    // Please note that something might need to be done with desiredOutput
    // before it is fed into dynInitializeB
    for (auto& ld : subLoads) {
        ld->dynInitializeB(inputs, desiredOutput, fieldSet);
    }
}

void aggregateLoad::set(std::string_view param, std::string_view val)
{
    std::string iparam;
    int num = trailingStringInt(param, iparam, -1);
    double frac = -1.0;
    if (iparam == "subload") {
        zipLoad* Ld;
        auto strSplit = splitline(val);
        trim(strSplit);
        auto load_factory = coreObjectFactory::instance()->getFactory("load");
        size_t nn = 0;
        while (nn < strSplit.size()) {
            if (load_factory->isValidType(strSplit[nn])) {
                Ld = static_cast<zipLoad*>(load_factory->makeObject(strSplit[nn]));
                Ld->locIndex = num;
                add(Ld);
                ++nn;
                try {
                    if (nn < strSplit.size()) {
                        frac = std::stod(strSplit[nn]);
                        ++nn;
                    } else {
                        frac = -1.0;
                    }
                }
                catch (const std::invalid_argument&) {
                    frac = -1.0;
                }
                if (num > 0) {
                    if (static_cast<int>(fraction.size()) < num) {
                        fraction.resize(num, -1);
                    }
                    fraction[num] = frac;
                } else {
                    fraction.push_back(frac);
                }
            }
        }
    } else if (param == "fraction") {
        auto fval = gmlc::utilities::str2vector<double>(std::string{val}, -1.0);
        for (size_t nn = 0; nn < fval.size(); ++nn) {
            if (nn + 1 >= fraction.size()) {
                logging::warning(this, "fraction specification count exceeds load count");
                break;
            }
            fraction[nn + 1] = fval[nn];
        }
        if (opFlags[pFlow_initialized]) {
            for (size_t nn = 0; nn < subLoads.size(); ++nn) {
                subLoads[nn]->set("p", getP() * fraction[nn]);
                subLoads[nn]->set("q", getQ() * fraction[nn]);
                subLoads[nn]->set("ir", getIp() * fraction[nn]);
                subLoads[nn]->set("iq", getIq() * fraction[nn]);
                subLoads[nn]->set("yp", getYp() * fraction[nn]);
                subLoads[nn]->set("Yq", getYq() * fraction[nn]);
            }
        }
    } else {
        zipLoad::set(param, val);
    }
}
void aggregateLoad::set(std::string_view param, double val, units::unit unitType)
{
    std::string iparam;
    int num = trailingStringInt(param, iparam, -1);
    bool reallocate = true;
    if (param == "consume") {
        consumeSimpleLoad = (val > 0);
        reallocate = false;
    } else if (iparam == "fraction") {
        if (num <= 0) {
            num = 1;
        }
        if (num >= static_cast<int>(fraction.size())) {
            logging::warning(this, "fraction specification count exceeds load count");
            throw(invalidParameterValue(param));
        }
        fraction[num] = val;
    } else {
        zipLoad::set(param, val, unitType);
    }
    if ((reallocate) && (opFlags[pFlow_initialized])) {
        for (size_t nn = 0; nn < subLoads.size(); ++nn) {
            subLoads[nn]->set("p", getP() * fraction[nn]);
            subLoads[nn]->set("q", getQ() * fraction[nn]);
            subLoads[nn]->set("ir", getIp() * fraction[nn]);
            subLoads[nn]->set("iq", getIq() * fraction[nn]);
            subLoads[nn]->set("yp", getYp() * fraction[nn]);
            subLoads[nn]->set("yq", getYq() * fraction[nn]);
        }
    }
}

void aggregateLoad::residual(const IOdata& inputs,
                             const stateData& sD,
                             double resid[],
                             const solverMode& sMode)
{
    for (auto& ld : subLoads) {
        if (ld->stateSize(sMode) > 0) {
            ld->residual(inputs, sD, resid, sMode);
        }
    }
}

void aggregateLoad::derivative(const IOdata& inputs,
                               const stateData& sD,
                               double deriv[],
                               const solverMode& sMode)
{
    for (auto& ld : subLoads) {
        if (ld->diffSize(sMode) > 0) {
            ld->derivative(inputs, sD, deriv, sMode);
        }
    }
}

void aggregateLoad::outputPartialDerivatives(const IOdata& inputs,
                                             const stateData& sD,
                                             matrixData<double>& md,
                                             const solverMode& sMode)
{
    for (auto& ld : subLoads) {
        if (ld->stateSize(sMode) > 0) {
            ld->outputPartialDerivatives(inputs, sD, md, sMode);
        }
    }
}

void aggregateLoad::ioPartialDerivatives(const IOdata& inputs,
                                         const stateData& sD,
                                         matrixData<double>& md,
                                         const IOlocs& inputLocs,
                                         const solverMode& sMode)
{
    for (auto& ld : subLoads) {
        ld->ioPartialDerivatives(inputs, sD, md, inputLocs, sMode);
    }
}

void aggregateLoad::jacobianElements(const IOdata& inputs,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
{
    for (auto& ld : subLoads) {
        if (ld->stateSize(sMode) > 0) {
            ld->jacobianElements(inputs, sD, md, inputLocs, sMode);
        }
    }
}

void aggregateLoad::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    for (auto& ld : subLoads) {
        ld->timestep(time, inputs, sMode);
    }
}

double aggregateLoad::getRealPower(const IOdata& inputs,
                                   const stateData& sD,
                                   const solverMode& sMode) const
{
    double rp = 0;
    for (auto& ld : subLoads) {
        if (ld->isConnected()) {
            rp += ld->getRealPower(inputs, sD, sMode);
        }
    }
    return rp;
}

double aggregateLoad::getReactivePower(const IOdata& inputs,
                                       const stateData& sD,
                                       const solverMode& sMode) const
{
    double rp = 0;
    for (auto& ld : subLoads) {
        if (ld->isConnected()) {
            rp += ld->getReactivePower(inputs, sD, sMode);
        }
    }
    return rp;
}

double aggregateLoad::getRealPower(double V) const
{
    double rp = 0;
    for (auto& ld : subLoads) {
        if (ld->isConnected()) {
            rp += ld->getRealPower(V);
        }
    }
    return rp;
}

double aggregateLoad::getReactivePower(double V) const
{
    double rp = 0;
    for (auto& ld : subLoads) {
        if (ld->isConnected()) {
            rp += ld->getReactivePower(V);
        }
    }
    return rp;
}

double aggregateLoad::getRealPower() const
{
    double rp = 0;
    for (auto& ld : subLoads) {
        if (ld->isConnected()) {
            rp += ld->getRealPower();
        }
    }
    return rp;
}

double aggregateLoad::getReactivePower() const
{
    double rp = 0;
    for (auto& ld : subLoads) {
        if (ld->isConnected()) {
            rp += ld->getReactivePower();
        }
    }
    return rp;
}
}  // namespace griddyn::loads
