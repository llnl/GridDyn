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

        constexpr lookupEntry(std::string_view entryName, Value entryValue):
            name(entryName), value(entryValue)
        {
        }
    };

    constexpr char asciiToLower(char character) noexcept
    {
        return ((character >= 'A') && (character <= 'Z')) ?
            static_cast<char>(character - 'A' + 'a') :
            character;
    }

    bool isLowerAscii(std::string_view str) noexcept
    {
        return std::all_of(str.begin(), str.end(), [](char character) {
            return (character < 'A') || (character > 'Z');
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

std::unique_ptr<std::mt19937> gridRandom::mGenerator;

bool gridRandom::mSeeded = false;
unsigned int gridRandom::mActualSeed = 0;

gridRandom::gridRandom(DistributionType dist, double param1, double param2):
    mDist(dist), mParam1(param1), mParam2(param2)
{
    setDistribution(dist);
    if (!mSeeded) {
        setSeed();
    }
}

gridRandom::gridRandom(std::string_view dist_name, double param1, double param2):
    gridRandom(getDist(dist_name), param1, param2)
{
}

void gridRandom::ensureEngine()
{
    if (!mGenerator) {
        mGenerator = std::make_unique<std::mt19937>();
    }
}

void gridRandom::setParameters(double param1, double param2)
{
    mParam1 = param1;
    mParam2 = param2;
    mDistribution->updateParameters(mParam1, mParam2);
}

void gridRandom::setSeed(unsigned int seed)
{
    ensureEngine();
    mActualSeed = seed;
    mGenerator->seed(seed);
    mSeeded = true;
}
void gridRandom::setSeed()
{
    ensureEngine();
    mActualSeed = static_cast<unsigned int>(time(nullptr));
    mGenerator->seed(mActualSeed);
    mSeeded = true;
}

unsigned int gridRandom::getSeed()
{
    return mActualSeed;
}
void gridRandom::setDistribution(DistributionType dist)
{
    mDist = dist;
    switch (dist) {
        case DistributionType::CONSTANT:
            mDistribution = std::make_unique<randomDistributionObject1<void>>(mParam1);
            break;
        case DistributionType::EXPONENTIAL:
            mDistribution =
                std::make_unique<randomDistributionObject1<std::exponential_distribution<double>>>(
                    1.0 / mParam1);
            break;
        case DistributionType::EXTREME_VALUE:
            mDistribution = std::make_unique<
                randomDistributionObject2<std::extreme_value_distribution<double>>>(mParam1,
                                                                                    mParam2);
            break;
        case DistributionType::GAMMA:
            mDistribution =
                std::make_unique<randomDistributionObject2<std::gamma_distribution<double>>>(
                    mParam1, mParam2);
            break;
        case DistributionType::NORMAL:
            mDistribution =
                std::make_unique<randomDistributionObject2<std::normal_distribution<double>>>(
                    mParam1, mParam2);
            break;
        case DistributionType::UNIFORM:
            mDistribution =
                std::make_unique<randomDistributionObject2<std::uniform_real_distribution<double>>>(
                    mParam1, mParam2);
            break;
        case DistributionType::LOGNORMAL:
            mDistribution =
                std::make_unique<randomDistributionObject2<std::lognormal_distribution<double>>>(
                    mParam1, mParam2);
            break;
        case DistributionType::UNIFORM_INT:
            mDistribution =
                std::make_unique<randomDistributionObject2<std::uniform_int_distribution<int>>>(
                    static_cast<int>(mParam1), static_cast<int>(mParam2));
            break;
    }
}

double gridRandom::randNumber(DistributionType dist)
{
    if (!mSeeded) {
        setSeed();
    }
    auto& engine = getEngine();
    switch (dist) {
        case DistributionType::CONSTANT:
        default:
            return 0.0;
            break;
        case DistributionType::GAMMA:
            return std::gamma_distribution<double>{}(engine);
            break;
        case DistributionType::EXTREME_VALUE:
            return std::extreme_value_distribution<double>{}(engine);
            break;
        case DistributionType::EXPONENTIAL:
            return std::exponential_distribution<double>{}(engine);
            break;
        case DistributionType::NORMAL:
            return std::normal_distribution<double>{}(engine);
            break;
        case DistributionType::UNIFORM:
            return std::uniform_real_distribution<double>{}(engine);
            break;
        case DistributionType::LOGNORMAL:
            return std::lognormal_distribution<double>{}(engine);
        case DistributionType::UNIFORM_INT:
            return static_cast<double>(std::uniform_int_distribution<int>{}(engine));
            break;
    }
}

double gridRandom::randNumber(DistributionType dist, double param1, double param2)
{
    if (!mSeeded) {
        setSeed();
    }
    auto& engine = getEngine();
    switch (dist) {
        case DistributionType::CONSTANT:
            return param1;
            break;
        case DistributionType::GAMMA:
            return std::gamma_distribution<double>{}(
                engine, std::gamma_distribution<double>::param_type(param1, param2));
            break;
        case DistributionType::EXTREME_VALUE:
            return std::extreme_value_distribution<double>{}(
                engine, std::extreme_value_distribution<double>::param_type(param1, param2));
            break;
        case DistributionType::EXPONENTIAL:
            return std::exponential_distribution<double>{}(
                engine, std::exponential_distribution<double>::param_type(1.0 / param1));
            break;
        case DistributionType::NORMAL:
            return std::normal_distribution<double>{}(
                engine, std::normal_distribution<double>::param_type(param1, param2));
            break;
        case DistributionType::UNIFORM:
            return std::uniform_real_distribution<double>{}(
                engine, std::uniform_real_distribution<double>::param_type(param1, param2));
            break;
        case DistributionType::LOGNORMAL:
            return std::lognormal_distribution<double>{}(
                engine, std::lognormal_distribution<double>::param_type(param1, param2));
            break;
        case DistributionType::UNIFORM_INT:
            return static_cast<double>(std::uniform_int_distribution<int>{}(
                engine,
                std::uniform_int_distribution<int>::param_type(static_cast<int>(param1),
                                                               static_cast<int>(param2))));
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
    return (*mDistribution)();
}
std::vector<double> gridRandom::getNewValues(size_t count)
{
    std::vector<double> randomValues(count);
    std::generate(randomValues.begin(), randomValues.end(), [this]() {
        return (*mDistribution)();
    });
    return randomValues;
}

void gridRandom::getNewValues(std::vector<double>& rvec, size_t count)
{
    gmlc::utilities::ensureSizeAtLeast(rvec, count);
    std::generate(rvec.begin(), rvec.begin() + count - 1, [this]() { return (*mDistribution)(); });
}

std::pair<double, double> gridRandom::getPair()
{
    return std::make_pair((*mDistribution)(), (*mDistribution)());
}
static constexpr auto distmap = makeLookupTable<gridRandom::DistributionType>({
    {"constant", gridRandom::DistributionType::CONSTANT},
    {"const", gridRandom::DistributionType::CONSTANT},
    {"uniform", gridRandom::DistributionType::UNIFORM},
    {"lognormal", gridRandom::DistributionType::LOGNORMAL},
    {"extreme", gridRandom::DistributionType::EXTREME_VALUE},
    {"exponential", gridRandom::DistributionType::EXPONENTIAL},
    {"gamma", gridRandom::DistributionType::GAMMA},
    {"normal", gridRandom::DistributionType::NORMAL},
    {"gaussian", gridRandom::DistributionType::NORMAL},
    {"uniform_int", gridRandom::DistributionType::UNIFORM_INT},
});

gridRandom::DistributionType getDist(std::string_view dist_name)
{
    return lookupValue(distmap, dist_name, gridRandom::DistributionType::CONSTANT);
}

}  // namespace utilities
