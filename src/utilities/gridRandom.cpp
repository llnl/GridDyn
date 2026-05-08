/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridRandom.h"

#include "gmlc/utilities/vectorOps.hpp"
#include <algorithm>
#include <array>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace utilities {
namespace {
    template<class Value>
    struct lookupEntry {
        std::string_view name;
        Value value;
    };

    constexpr char asciiToLower(char ch) noexcept
    {
        return ((ch >= 'A') && (ch <= 'Z')) ? static_cast<char>(ch - 'A' + 'a') : ch;
    }

    bool isLowerAscii(std::string_view str) noexcept
    {
        return std::all_of(str.begin(), str.end(), [](char ch) {
            return (ch < 'A') || (ch > 'Z');
        });
    }

    template<class Value, std::size_t N>
    constexpr auto makeLookupTable(const lookupEntry<Value> (&entries)[N])
    {
        return std::to_array(entries);
    }

    template<class Value, std::size_t N>
    Value lookupExact(const std::array<lookupEntry<Value>, N>& lookupTable,
                      std::string_view key,
                      Value defaultValue)
    {
        const auto match = std::find_if(lookupTable.begin(),
                                        lookupTable.end(),
                                        [key](const auto& entry) { return entry.name == key; });
        return (match != lookupTable.end()) ? match->value : defaultValue;
    }

    template<class Value, std::size_t N>
    Value lookupValue(const std::array<lookupEntry<Value>, N>& lookupTable,
                      std::string_view key,
                      Value defaultValue)
    {
        if (isLowerAscii(key)) {
            return lookupExact(lookupTable, key, defaultValue);
        }
        std::string lowerKey(key);
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), asciiToLower);
        return lookupExact(lookupTable, lowerKey, defaultValue);
    }
}  // namespace

std::mt19937 gridRandom::s_gen;
std::uniform_real_distribution<double> gridRandom::s_udist;
std::exponential_distribution<double> gridRandom::s_expdist;
std::normal_distribution<double> gridRandom::s_normdist;
std::lognormal_distribution<double> gridRandom::s_lnormdist;
std::gamma_distribution<double> gridRandom::s_gammadist;
std::extreme_value_distribution<double> gridRandom::s_evdist;
std::uniform_int_distribution<int> gridRandom::s_uintdist;

bool gridRandom::seeded = false;
unsigned int gridRandom::actual_seed = 0;

gridRandom::gridRandom(dist_type_t dist, double param1, double param2):
    m_dist(dist), param1_(param1), param2_(param2)
{
    setDistribution(dist);
    if (!seeded) {
        setSeed();
    }
}

gridRandom::gridRandom(std::string_view dist_name, double param1, double param2):
    gridRandom(getDist(dist_name), param1, param2)
{
}

void gridRandom::setParameters(double param1, double param2)
{
    param1_ = param1;
    param2_ = param2;
    dobj->updateParameters(param1_, param2_);
}

void gridRandom::setSeed(unsigned int seed)
{
    actual_seed = seed;
    s_gen.seed(seed);
    seeded = true;
}
void gridRandom::setSeed()
{
    actual_seed = static_cast<unsigned int>(time(nullptr));
    s_gen.seed(actual_seed);
    seeded = true;
}

unsigned int gridRandom::getSeed()
{
    return actual_seed;
}
void gridRandom::setDistribution(dist_type_t dist)
{
    m_dist = dist;
    switch (dist) {
        case dist_type_t::constant:
            dobj = std::make_unique<randomDistributionObject1<void>>(param1_);
            break;
        case dist_type_t::exponential:
            dobj =
                std::make_unique<randomDistributionObject1<std::exponential_distribution<double>>>(
                    1.0 / param1_);
            break;
        case dist_type_t::extreme_value:
            dobj = std::make_unique<
                randomDistributionObject2<std::extreme_value_distribution<double>>>(param1_,
                                                                                    param2_);
            break;
        case dist_type_t::gamma:
            dobj = std::make_unique<randomDistributionObject2<std::gamma_distribution<double>>>(
                param1_, param2_);
            break;
        case dist_type_t::normal:
            dobj = std::make_unique<randomDistributionObject2<std::normal_distribution<double>>>(
                param1_, param2_);
            break;
        case dist_type_t::uniform:
            dobj =
                std::make_unique<randomDistributionObject2<std::uniform_real_distribution<double>>>(
                    param1_, param2_);
            break;
        case dist_type_t::lognormal:
            dobj = std::make_unique<randomDistributionObject2<std::lognormal_distribution<double>>>(
                param1_, param2_);
            break;
        case dist_type_t::uniform_int:
            dobj = std::make_unique<randomDistributionObject2<std::uniform_int_distribution<int>>>(
                static_cast<int>(param1_), static_cast<int>(param2_));
            break;
    }
}

double gridRandom::randNumber(dist_type_t dist)
{
    if (!seeded) {
        setSeed();
    }
    switch (dist) {
        case dist_type_t::constant:
        default:
            return 0.0;
            break;
        case dist_type_t::gamma:
            return s_gammadist(s_gen);
            break;
        case dist_type_t::extreme_value:
            return s_evdist(s_gen);
            break;
        case dist_type_t::exponential:
            return s_expdist(s_gen);
            break;
        case dist_type_t::normal:
            return s_normdist(s_gen);
            break;
        case dist_type_t::uniform:
            return s_udist(s_gen);
            break;
        case dist_type_t::lognormal:
            return s_lnormdist(s_gen);
        case dist_type_t::uniform_int:
            return static_cast<double>(s_uintdist(s_gen));
            break;
    }
}

double gridRandom::randNumber(dist_type_t dist, double param1, double param2)
{
    if (!seeded) {
        setSeed();
    }
    switch (dist) {
        case dist_type_t::constant:
            return param1;
            break;
        case dist_type_t::gamma:
            return s_gammadist(s_gen, std::gamma_distribution<double>::param_type(param1, param2));
            break;
        case dist_type_t::extreme_value:
            return s_evdist(s_gen,
                            std::extreme_value_distribution<double>::param_type(param1, param2));
            break;
        case dist_type_t::exponential:
            return s_expdist(s_gen,
                             std::exponential_distribution<double>::param_type(1.0 / param1));
            break;
        case dist_type_t::normal:
            return s_normdist(s_gen, std::normal_distribution<double>::param_type(param1, param2));
            break;
        case dist_type_t::uniform:
            return s_udist(s_gen,
                           std::uniform_real_distribution<double>::param_type(param1, param2));
            break;
        case dist_type_t::lognormal:
            return s_lnormdist(s_gen,
                               std::lognormal_distribution<double>::param_type(param1, param2));
            break;
        case dist_type_t::uniform_int:
            return static_cast<double>(
                s_uintdist(s_gen,
                           std::uniform_int_distribution<int>::param_type(
                               static_cast<int>(param1), static_cast<int>(param2))));
        default:
            return 0.0;
    }
}

double gridRandom::operator()()
{
    return generate();
}
double gridRandom::generate()
{
    return (*dobj)();
}
std::vector<double> gridRandom::getNewValues(size_t count)
{
    std::vector<double> rv(count);
    std::generate(rv.begin(), rv.end(), [this]() { return (*dobj)(); });
    return rv;
}

void gridRandom::getNewValues(std::vector<double>& rvec, size_t count)
{
    gmlc::utilities::ensureSizeAtLeast(rvec, count);
    std::generate(rvec.begin(), rvec.begin() + count - 1, [this]() { return (*dobj)(); });
}

std::pair<double, double> gridRandom::getPair()
{
    return std::make_pair((*dobj)(), (*dobj)());
}
static constexpr auto distmap = makeLookupTable<gridRandom::dist_type_t>({
    {"constant", gridRandom::dist_type_t::constant},
    {"const", gridRandom::dist_type_t::constant},
    {"uniform", gridRandom::dist_type_t::uniform},
    {"lognormal", gridRandom::dist_type_t::lognormal},
    {"extreme", gridRandom::dist_type_t::extreme_value},
    {"exponential", gridRandom::dist_type_t::exponential},
    {"gamma", gridRandom::dist_type_t::gamma},
    {"normal", gridRandom::dist_type_t::normal},
    {"gaussian", gridRandom::dist_type_t::normal},
    {"uniform_int", gridRandom::dist_type_t::uniform_int},
});

gridRandom::dist_type_t getDist(std::string_view dist_name)
{
    return lookupValue(distmap, dist_name, gridRandom::dist_type_t::constant);
}

}  // namespace utilities
