/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SourceLoad.h"

#include "../Source.h"
#include "../sources/sourceTypes.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn::loads {
SourceLoad::SourceLoad(const std::string& objName): ZipLoad(objName), sourceLink{}
{
    sourceLink.fill(-1);
}
SourceLoad::SourceLoad(SourceType type, const std::string& objName): SourceLoad(objName)
{
    sType = type;
    // add the sources for P and Q
    SourceLoad::add(makeSource(P_SOURCE));
    SourceLoad::add(makeSource(Q_SOURCE));
}

CoreObject* SourceLoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<SourceLoad, ZipLoad>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->sourceLink = sourceLink;
    return nobj;
}

static const std::map<std::string_view, int, std::less<>> SOURCE_LOOKUP{
    {"source", SourceLoad::P_SOURCE},
    {"psource", SourceLoad::P_SOURCE},
    {"p_source", SourceLoad::P_SOURCE},
    {"qsource", SourceLoad::Q_SOURCE},
    {"q_source", SourceLoad::Q_SOURCE},
    {"resource", SourceLoad::R_SOURCE},
    {"r_source", SourceLoad::R_SOURCE},
    {"xsource", SourceLoad::X_SOURCE},
    {"x_source", SourceLoad::X_SOURCE},
    {"ypsource", SourceLoad::YP_SOURCE},
    {"yp_source", SourceLoad::YP_SOURCE},
    {"yqsource", SourceLoad::YQ_SOURCE},
    {"yq_source", SourceLoad::YQ_SOURCE},
    {"ipsource", SourceLoad::IP_SOURCE},
    {"ip_source", SourceLoad::IP_SOURCE},
    {"iqsource", SourceLoad::IQ_SOURCE},
    {"iq_source", SourceLoad::IQ_SOURCE},
};

static const std::map<std::string_view, int, std::less<>> SOURCEKEY_LOOKUP{
    {"p", SourceLoad::P_SOURCE},
    {"q", SourceLoad::Q_SOURCE},
    {"r", SourceLoad::R_SOURCE},
    {"x", SourceLoad::X_SOURCE},
    {"yp", SourceLoad::YP_SOURCE},
    {"zp", SourceLoad::YP_SOURCE},
    {"zr", SourceLoad::YP_SOURCE},
    {"yq", SourceLoad::YQ_SOURCE},
    {"zq", SourceLoad::YQ_SOURCE},
    {"ip", SourceLoad::IP_SOURCE},
    {"iq", SourceLoad::IQ_SOURCE},
};

static const std::map<std::string_view, int, std::less<>> SOURCE_MATCH{
    {"source", SourceLoad::P_SOURCE},     {"psource", SourceLoad::P_SOURCE},
    {"p_source", SourceLoad::P_SOURCE},   {"qsource", SourceLoad::Q_SOURCE},
    {"q_source", SourceLoad::Q_SOURCE},   {"resource", SourceLoad::R_SOURCE},
    {"r_source", SourceLoad::R_SOURCE},   {"xsource", SourceLoad::X_SOURCE},
    {"x_source", SourceLoad::X_SOURCE},   {"ypsource", SourceLoad::YP_SOURCE},
    {"yp_source", SourceLoad::YP_SOURCE}, {"yqsource", SourceLoad::YQ_SOURCE},
    {"yq_source", SourceLoad::YQ_SOURCE}, {"ipsource", SourceLoad::IP_SOURCE},
    {"ip_source", SourceLoad::IP_SOURCE}, {"iqsource", SourceLoad::IQ_SOURCE},
    {"iq_source", SourceLoad::IQ_SOURCE}, {"p", SourceLoad::P_SOURCE},
    {"q", SourceLoad::Q_SOURCE},          {"r", SourceLoad::R_SOURCE},
    {"x", SourceLoad::X_SOURCE},          {"yp", SourceLoad::YP_SOURCE},
    {"zp", SourceLoad::YP_SOURCE},        {"zr", SourceLoad::YP_SOURCE},
    {"yq", SourceLoad::YQ_SOURCE},        {"zq", SourceLoad::YQ_SOURCE},
    {"ip", SourceLoad::IP_SOURCE},        {"iq", SourceLoad::IQ_SOURCE},
};

void SourceLoad::add(CoreObject* obj)
{
    if (dynamic_cast<Source*>(obj) != nullptr) {
        add(static_cast<Source*>(obj));
    }
}

void SourceLoad::add(Source* src)
{
    src->setParent(this);
    src->setFlag("pflow_init_required", true);
    if (src->locIndex != kNullLocation) {
    } else if (!src->purpose_.empty()) {
        auto ind = SOURCE_MATCH.find(std::string_view{src->purpose_});
        if (ind != SOURCE_MATCH.end()) {
            src->locIndex = ind->second;
        } else {
            src->locIndex = static_cast<int>(sources.size());
        }
    } else {
        src->locIndex = static_cast<int>(sources.size());
    }

    if (std::cmp_less_equal(sources.size(), src->locIndex)) {
        sources.resize(src->locIndex + 1, nullptr);
    }
    if (sources[src->locIndex] != nullptr) {
        remove(sources[src->locIndex]);
    }
    sources[src->locIndex] = src;
    if (src->locIndex < 8) {
        if (sourceLink[src->locIndex] < 0) {
            sourceLink[src->locIndex] = static_cast<int>(src->locIndex);
        }
    }
    // now add to the subObjectList
    addSubObject(src);
}

void SourceLoad::remove(CoreObject* obj)
{
    if (dynamic_cast<Source*>(obj) != nullptr) {
        remove(static_cast<Source*>(obj));
    } else {
        GridSecondary::remove(obj);
    }
}

void SourceLoad::remove(Source* src)
{
    if (src == nullptr) {
        return;
    }
    if ((src->locIndex != kNullLocation) && std::cmp_less(src->locIndex, sources.size())) {
        if (isSameObject(sources[src->locIndex], src)) {
            sources[src->locIndex] = nullptr;
            src->setParent(nullptr);
            for (auto& lnk : sourceLink) {
                if (lnk == static_cast<int>(src->locIndex)) {
                    lnk = -1;
                }
            }
            GridSecondary::remove(src);
        }
    }
}

Source* SourceLoad::findSource(std::string_view srcname)
{
    auto ind = SOURCE_MATCH.find(srcname);
    if (ind != SOURCE_MATCH.end()) {
        const int index = sourceLink[ind->second];
        if ((index < 0) || std::cmp_less_equal(sources.size(), index) ||
            (sources[index] == nullptr)) {
            // this may not actually do anything is the sType is set to other
            add(makeSource(static_cast<SourceLoad::SourceLoc>(ind->second)));
        }
        const int updatedIndex = sourceLink[ind->second];
        return (updatedIndex >= 0 && std::cmp_greater(sources.size(), updatedIndex)) ?
            sources[updatedIndex] :
            nullptr;
    }
    return nullptr;
}

Source* SourceLoad::findSource(std::string_view srcname) const
{
    auto ind = SOURCE_MATCH.find(srcname);
    if (ind != SOURCE_MATCH.end()) {
        const int index = sourceLink[ind->second];
        if ((index < 0) || std::cmp_less_equal(sources.size(), index)) {
            return nullptr;
        }
        return sources[index];
    }
    return nullptr;
}

void SourceLoad::setFlag(std::string_view flag, bool val)
{
    auto sfnd = flag.find_last_of(":?");
    if (sfnd != std::string::npos) {
        auto* src = findSource(flag.substr(0, sfnd));
        if (src != nullptr) {
            src->setFlag(flag.substr(sfnd + 1, std::string::npos), val);
        } else {
            throw(UnrecognizedParameter(flag));
        }
    } else {
        ZipLoad::setFlag(flag, val);
    }
}

void SourceLoad::set(std::string_view param, std::string_view val)
{
    auto sfnd = param.find_last_of(":?");
    if (sfnd != std::string::npos) {
        auto* src = findSource(param.substr(0, sfnd));
        if (src != nullptr) {
            src->set(param.substr(sfnd + 1, std::string::npos), val);
        } else {
            throw(UnrecognizedParameter(param));
        }
    } else {
        ZipLoad::set(param, val);
    }
}

void SourceLoad::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    for (const auto& src : getSubObjects()) {
        static_cast<Source*>(src)->timestep(time, noInputs, sMode);
    }
    getSourceLoads();
    prevTime = time;
    ZipLoad::timestep(time, inputs, sMode);
}

void SourceLoad::setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode)
{
    for (const auto& src : getSubObjects()) {
        src->setState(time, state, dstateDt, sMode);
    }
    getSourceLoads();
    prevTime = time;
}

void SourceLoad::set(std::string_view param, double val, units::unit unitType)
{
    auto sfnd = param.find_last_of(":?");
    if (sfnd != std::string::npos) {
        auto* src = findSource(param.substr(0, sfnd));
        if (src != nullptr) {
            src->set(param.substr(sfnd + 1, std::string::npos), val, unitType);
        } else {
            throw(UnrecognizedParameter(param));
        }
    } else {
        auto ind = SOURCE_LOOKUP.find(param);
        if (ind != SOURCE_LOOKUP.end()) {
            const bool canSetSourceLink = (std::cmp_greater(sources.size(), ind->second) &&
                                           (sources[ind->second] != nullptr)) ||
                (!opFlags[POWERFLOW_INITIALIZED]);
            if (canSetSourceLink) {
                sourceLink[ind->second] = static_cast<int>(val);
            } else {
                throw(UnrecognizedParameter(param));
            }
            return;
        }

        auto keyind = SOURCEKEY_LOOKUP.find(param);

        if (keyind != SOURCEKEY_LOOKUP.end()) {
            if (std::cmp_greater(sources.size(), keyind->second) &&
                (sources[keyind->second] != nullptr)) {
                sources[keyind->second]->set(
                    "level", units::convert(val, unitType, units::puMW, systemBasePower));
                return;
            }
        }
        ZipLoad::set(param, val, unitType);
    }
}

void SourceLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    // Do a check on the sources;
    for (auto& sourceLocation : sourceLink) {
        if (sourceLocation < 0) {
            continue;
        }
        if (std::cmp_greater_equal(sourceLocation, sources.size()) ||
            (sources[sourceLocation] == nullptr)) {
            logging::warning(this, "no source given at called index");
        }
    }
    ZipLoad::pFlowObjectInitializeA(time0, flags);  // to initialize the submodels

    getSourceLoads();
}

void SourceLoad::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    ZipLoad::dynObjectInitializeA(time0, flags);
    getSourceLoads();
}

void SourceLoad::updateLocalCache(const IOdata& /*inputs*/,
                                  const StateData& stateDataValue,
                                  const SolverMode& sMode)
{
    for (auto& src : sources) {
        src->updateLocalCache(noInputs, stateDataValue, sMode);
    }
    getSourceLoads();
}

void SourceLoad::getSourceLoads()
{
    if (sourceLink[P_SOURCE] >= 0) {
        setP(sources[sourceLink[P_SOURCE]]->getOutput());
    }
    if (sourceLink[Q_SOURCE] >= 0) {
        setQ(sources[sourceLink[Q_SOURCE]]->getOutput());
    }
    if (sourceLink[YP_SOURCE] >= 0) {
        setup(sources[sourceLink[YP_SOURCE]]->getOutput());
    }
    if (sourceLink[YQ_SOURCE] >= 0) {
        setYq(sources[sourceLink[YQ_SOURCE]]->getOutput());
    }
    if (sourceLink[IP_SOURCE] >= 0) {
        setIp(sources[sourceLink[IP_SOURCE]]->getOutput());
    }
    if (sourceLink[IQ_SOURCE] >= 0) {
        setIq(sources[sourceLink[IQ_SOURCE]]->getOutput());
    }
    if (sourceLink[R_SOURCE] >= 0) {
        setr(sources[sourceLink[R_SOURCE]]->getOutput());
    }
    if (sourceLink[X_SOURCE] >= 0) {
        setx(sources[sourceLink[X_SOURCE]]->getOutput());
    }
}

Source* SourceLoad::makeSource(SourceLoc loc)
{
    Source* src = nullptr;
    switch (sType) {
        case SourceType::PULSE:
            src = new sources::PulseSource();
            break;
        case SourceType::RANDOM:
            src = new sources::RandomSource();
            break;
        case SourceType::SINE:
            src = new sources::SineSource();
            break;
        case SourceType::OTHER:
        default:
            return nullptr;
    }
    src->locIndex = static_cast<index_t>(loc);
    return src;
}

CoreObject* SourceLoad::find(std::string_view obj) const
{
    auto* src = findSource(obj);
    if (src == nullptr) {
        return GridComponent::find(obj);
    }
    return src;
}
}  // namespace griddyn::loads
