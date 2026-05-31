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
    SourceLoad::add(makeSource(p_source));
    SourceLoad::add(makeSource(q_source));
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
    {"source", SourceLoad::p_source},
    {"psource", SourceLoad::p_source},
    {"p_source", SourceLoad::p_source},
    {"qsource", SourceLoad::q_source},
    {"q_source", SourceLoad::q_source},
    {"resource", SourceLoad::r_source},
    {"r_source", SourceLoad::r_source},
    {"xsource", SourceLoad::x_source},
    {"x_source", SourceLoad::x_source},
    {"ypsource", SourceLoad::yp_source},
    {"yp_source", SourceLoad::yp_source},
    {"yqsource", SourceLoad::yq_source},
    {"yq_source", SourceLoad::yq_source},
    {"ipsource", SourceLoad::ip_source},
    {"ip_source", SourceLoad::ip_source},
    {"iqsource", SourceLoad::iq_source},
    {"iq_source", SourceLoad::iq_source},
};

static const std::map<std::string_view, int, std::less<>> SOURCEKEY_LOOKUP{
    {"p", SourceLoad::p_source},
    {"q", SourceLoad::q_source},
    {"r", SourceLoad::r_source},
    {"x", SourceLoad::x_source},
    {"yp", SourceLoad::yp_source},
    {"zp", SourceLoad::yp_source},
    {"zr", SourceLoad::yp_source},
    {"yq", SourceLoad::yq_source},
    {"zq", SourceLoad::yq_source},
    {"ip", SourceLoad::ip_source},
    {"iq", SourceLoad::iq_source},
};

static const std::map<std::string_view, int, std::less<>> SOURCE_MATCH{
    {"source", SourceLoad::p_source},     {"psource", SourceLoad::p_source},
    {"p_source", SourceLoad::p_source},   {"qsource", SourceLoad::q_source},
    {"q_source", SourceLoad::q_source},   {"resource", SourceLoad::r_source},
    {"r_source", SourceLoad::r_source},   {"xsource", SourceLoad::x_source},
    {"x_source", SourceLoad::x_source},   {"ypsource", SourceLoad::yp_source},
    {"yp_source", SourceLoad::yp_source}, {"yqsource", SourceLoad::yq_source},
    {"yq_source", SourceLoad::yq_source}, {"ipsource", SourceLoad::ip_source},
    {"ip_source", SourceLoad::ip_source}, {"iqsource", SourceLoad::iq_source},
    {"iq_source", SourceLoad::iq_source}, {"p", SourceLoad::p_source},
    {"q", SourceLoad::q_source},          {"r", SourceLoad::r_source},
    {"x", SourceLoad::x_source},          {"yp", SourceLoad::yp_source},
    {"zp", SourceLoad::yp_source},        {"zr", SourceLoad::yp_source},
    {"yq", SourceLoad::yq_source},        {"zq", SourceLoad::yq_source},
    {"ip", SourceLoad::ip_source},        {"iq", SourceLoad::iq_source},
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
                (!opFlags[pFlow_initialized]);
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
    if (sourceLink[p_source] >= 0) {
        setP(sources[sourceLink[p_source]]->getOutput());
    }
    if (sourceLink[q_source] >= 0) {
        setQ(sources[sourceLink[q_source]]->getOutput());
    }
    if (sourceLink[yp_source] >= 0) {
        setup(sources[sourceLink[yp_source]]->getOutput());
    }
    if (sourceLink[yq_source] >= 0) {
        setYq(sources[sourceLink[yq_source]]->getOutput());
    }
    if (sourceLink[ip_source] >= 0) {
        setIp(sources[sourceLink[ip_source]]->getOutput());
    }
    if (sourceLink[iq_source] >= 0) {
        setIq(sources[sourceLink[iq_source]]->getOutput());
    }
    if (sourceLink[r_source] >= 0) {
        setr(sources[sourceLink[r_source]]->getOutput());
    }
    if (sourceLink[x_source] >= 0) {
        setx(sources[sourceLink[x_source]]->getOutput());
    }
}

Source* SourceLoad::makeSource(SourceLoc loc)
{
    Source* src = nullptr;
    switch (sType) {
        case SourceType::pulse:
            src = new sources::PulseSource();
            break;
        case SourceType::random:
            src = new sources::RandomSource();
            break;
        case SourceType::sine:
            src = new sources::SineSource();
            break;
        case SourceType::other:
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
