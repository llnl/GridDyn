/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sourceLoad.h"

#include "../Source.h"
#include "../sources/sourceTypes.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn::loads {
sourceLoad::sourceLoad(const std::string& objName): zipLoad(objName), sourceLink{}
{
    sourceLink.fill(-1);
}
sourceLoad::sourceLoad(sourceType type, const std::string& objName): sourceLoad(objName)
{
    sType = type;
    // add the sources for P and Q
    sourceLoad::add(makeSource(p_source));
    sourceLoad::add(makeSource(q_source));
}

coreObject* sourceLoad::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<sourceLoad, zipLoad>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->sourceLink = sourceLink;
    return nobj;
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string_view, int, std::less<>> source_lookup{
    {"source", sourceLoad::p_source},
    {"psource", sourceLoad::p_source},
    {"p_source", sourceLoad::p_source},
    {"qsource", sourceLoad::q_source},
    {"q_source", sourceLoad::q_source},
    {"resource", sourceLoad::r_source},
    {"r_source", sourceLoad::r_source},
    {"xsource", sourceLoad::x_source},
    {"x_source", sourceLoad::x_source},
    {"ypsource", sourceLoad::yp_source},
    {"yp_source", sourceLoad::yp_source},
    {"yqsource", sourceLoad::yq_source},
    {"yq_source", sourceLoad::yq_source},
    {"ipsource", sourceLoad::ip_source},
    {"ip_source", sourceLoad::ip_source},
    {"iqsource", sourceLoad::iq_source},
    {"iq_source", sourceLoad::iq_source},
};

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string_view, int, std::less<>> sourcekey_lookup{
    {"p", sourceLoad::p_source},
    {"q", sourceLoad::q_source},
    {"r", sourceLoad::r_source},
    {"x", sourceLoad::x_source},
    {"yp", sourceLoad::yp_source},
    {"zp", sourceLoad::yp_source},
    {"zr", sourceLoad::yp_source},
    {"yq", sourceLoad::yq_source},
    {"zq", sourceLoad::yq_source},
    {"ip", sourceLoad::ip_source},
    {"iq", sourceLoad::iq_source},
};

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string_view, int, std::less<>> source_match{
    {"source", sourceLoad::p_source},     {"psource", sourceLoad::p_source},
    {"p_source", sourceLoad::p_source},   {"qsource", sourceLoad::q_source},
    {"q_source", sourceLoad::q_source},   {"resource", sourceLoad::r_source},
    {"r_source", sourceLoad::r_source},   {"xsource", sourceLoad::x_source},
    {"x_source", sourceLoad::x_source},   {"ypsource", sourceLoad::yp_source},
    {"yp_source", sourceLoad::yp_source}, {"yqsource", sourceLoad::yq_source},
    {"yq_source", sourceLoad::yq_source}, {"ipsource", sourceLoad::ip_source},
    {"ip_source", sourceLoad::ip_source}, {"iqsource", sourceLoad::iq_source},
    {"iq_source", sourceLoad::iq_source}, {"p", sourceLoad::p_source},
    {"q", sourceLoad::q_source},          {"r", sourceLoad::r_source},
    {"x", sourceLoad::x_source},          {"yp", sourceLoad::yp_source},
    {"zp", sourceLoad::yp_source},        {"zr", sourceLoad::yp_source},
    {"yq", sourceLoad::yq_source},        {"zq", sourceLoad::yq_source},
    {"ip", sourceLoad::ip_source},        {"iq", sourceLoad::iq_source},
};

void sourceLoad::add(coreObject* obj)
{
    if (dynamic_cast<Source*>(obj) != nullptr) {
        add(static_cast<Source*>(obj));
    }
}

void sourceLoad::add(Source* src)
{
    src->setParent(this);
    src->setFlag("pflow_init_required", true);
    if (src->locIndex != kNullLocation) {
    } else if (!src->purpose_.empty()) {
        auto ind = source_match.find(std::string_view{src->purpose_});
        if (ind != source_match.end()) {
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

void sourceLoad::remove(coreObject* obj)
{
    if (dynamic_cast<Source*>(obj) != nullptr) {
        remove(static_cast<Source*>(obj));
    } else {
        gridSecondary::remove(obj);
    }
}

void sourceLoad::remove(Source* src)
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
            gridSecondary::remove(src);
        }
    }
}

Source* sourceLoad::findSource(std::string_view srcname)
{
    auto ind = source_match.find(srcname);
    if (ind != source_match.end()) {
        const int index = sourceLink[ind->second];
        if ((index < 0) || std::cmp_less_equal(sources.size(), index) ||
            (sources[index] == nullptr)) {
            // this may not actually do anything is the sType is set to other
            add(makeSource(static_cast<sourceLoad::sourceLoc>(ind->second)));
        }
        const int updatedIndex = sourceLink[ind->second];
        return (updatedIndex >= 0 && std::cmp_greater(sources.size(), updatedIndex)) ?
            sources[updatedIndex] :
            nullptr;
    }
    return nullptr;
}

Source* sourceLoad::findSource(std::string_view srcname) const
{
    auto ind = source_match.find(srcname);
    if (ind != source_match.end()) {
        const int index = sourceLink[ind->second];
        if ((index < 0) || std::cmp_less_equal(sources.size(), index)) {
            return nullptr;
        }
        return sources[index];
    }
    return nullptr;
}

void sourceLoad::setFlag(std::string_view flag, bool val)
{
    auto sfnd = flag.find_last_of(":?");
    if (sfnd != std::string::npos) {
        auto* src = findSource(flag.substr(0, sfnd));
        if (src != nullptr) {
            src->setFlag(flag.substr(sfnd + 1, std::string::npos), val);
        } else {
            throw(unrecognizedParameter(flag));
        }
    } else {
        zipLoad::setFlag(flag, val);
    }
}

void sourceLoad::set(std::string_view param, std::string_view val)
{
    auto sfnd = param.find_last_of(":?");
    if (sfnd != std::string::npos) {
        auto* src = findSource(param.substr(0, sfnd));
        if (src != nullptr) {
            src->set(param.substr(sfnd + 1, std::string::npos), val);
        } else {
            throw(unrecognizedParameter(param));
        }
    } else {
        zipLoad::set(param, val);
    }
}

void sourceLoad::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    for (const auto& src : getSubObjects()) {
        static_cast<Source*>(src)->timestep(time, noInputs, sMode);
    }
    getSourceLoads();
    prevTime = time;
    zipLoad::timestep(time, inputs, sMode);
}

void sourceLoad::setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode)
{
    for (const auto& src : getSubObjects()) {
        src->setState(time, state, dstate_dt, sMode);
    }
    getSourceLoads();
    prevTime = time;
}

void sourceLoad::set(std::string_view param, double val, units::unit unitType)
{
    auto sfnd = param.find_last_of(":?");
    if (sfnd != std::string::npos) {
        auto* src = findSource(param.substr(0, sfnd));
        if (src != nullptr) {
            src->set(param.substr(sfnd + 1, std::string::npos), val, unitType);
        } else {
            throw(unrecognizedParameter(param));
        }
    } else {
        auto ind = source_lookup.find(param);
        if (ind != source_lookup.end()) {
            const bool canSetSourceLink =
                (std::cmp_greater(sources.size(), ind->second) && (sources[ind->second] != nullptr)) ||
                (!opFlags[pFlow_initialized]);
            if (canSetSourceLink) {
                sourceLink[ind->second] = static_cast<int>(val);
            } else {
                throw(unrecognizedParameter(param));
            }
            return;
        }

        auto keyind = sourcekey_lookup.find(param);

        if (keyind != sourcekey_lookup.end()) {
            if (std::cmp_greater(sources.size(), keyind->second) &&
                (sources[keyind->second] != nullptr)) {
                sources[keyind->second]->set(
                    "level", units::convert(val, unitType, units::puMW, systemBasePower));
                return;
            }
        }
        zipLoad::set(param, val, unitType);
    }
}

void sourceLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
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
    zipLoad::pFlowObjectInitializeA(time0, flags);  // to initialize the submodels

    getSourceLoads();
}

void sourceLoad::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    zipLoad::dynObjectInitializeA(time0, flags);
    getSourceLoads();
}

void sourceLoad::updateLocalCache(const IOdata& /*inputs*/,
                                  const stateData& stateDataValue,
                                  const solverMode& sMode)
{
    for (auto& src : sources) {
        src->updateLocalCache(noInputs, stateDataValue, sMode);
    }
    getSourceLoads();
}

void sourceLoad::getSourceLoads()
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

Source* sourceLoad::makeSource(sourceLoc loc)
{
    Source* src = nullptr;
    switch (sType) {
        case sourceType::pulse:
            src = new sources::pulseSource();
            break;
        case sourceType::random:
            src = new sources::randomSource();
            break;
        case sourceType::sine:
            src = new sources::sineSource();
            break;
        case sourceType::other:
        default:
            return nullptr;
    }
    src->locIndex = static_cast<index_t>(loc);
    return src;
}

coreObject* sourceLoad::find(std::string_view obj) const
{
    auto* src = findSource(obj);
    if (src == nullptr) {
        return gridComponent::find(obj);
    }
    return src;
}
}  // namespace griddyn::loads
