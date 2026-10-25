/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// disable a funny warning (bug in visual studio 2015)
#ifdef _MSC_VER
#    if _MSC_VER >= 1900
#        pragma warning(disable : 4592)
#    endif
#endif

#include "functionInterpreter.h"

#include "GridRandom.h"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/griddyn-config.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static constexpr double localPi = std::numbers::pi_v<double>;
static constexpr double localNan = std::numeric_limits<double>::quiet_NaN();
static constexpr double localInf = 1e48;
static constexpr double log2val = std::numbers::ln2_v<double>;
static constexpr double log10val = std::numbers::ln10_v<double>;

using gmlc::utilities::absMax;
using gmlc::utilities::mult_sum;
using gmlc::utilities::stdev;
using gmlc::utilities::sum;
using utilities::GridRandom;

namespace {
template<class Fn>
struct FunctionEntry {
    std::string_view mName;
    Fn mFunction;

    constexpr FunctionEntry(std::string_view entryName, Fn entryFunction):
        mName(entryName), mFunction(entryFunction)
    {
    }
};

constexpr char asciiToLower(char character) noexcept
{
    return ((character >= 'A') && (character <= 'Z')) ? static_cast<char>(character - 'A' + 'a') :
                                                        character;
}

bool isLowerAscii(std::string_view str) noexcept
{
    return std::all_of(str.begin(), str.end(), [](char character) {
        return ((character < 'A') || (character > 'Z'));
    });
}

template<class Fn, std::size_t N>
Fn lookupFunctionExact(const std::array<FunctionEntry<Fn>, N>& functions,
                       std::string_view functionName)
{
    const auto functionMatch =
        std::find_if(functions.begin(), functions.end(), [functionName](const auto& entry) {
            return entry.mName == functionName;
        });
    return (functionMatch != functions.end()) ? functionMatch->mFunction : nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<FunctionEntry<function0_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<FunctionEntry<function1_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<FunctionEntry<function2_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<FunctionEntry<function3_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<FunctionEntry<vector_function1_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<FunctionEntry<vector_function2_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<class Fn, std::size_t N>
Fn lookupFunction(const std::array<FunctionEntry<Fn>, N>& functions, std::string_view functionName)
{
    if (isLowerAscii(functionName)) {
        return lookupFunctionExact(functions, functionName);
    }
    std::string lowerFunctionName(functionName);
    std::transform(lowerFunctionName.begin(),
                   lowerFunctionName.end(),
                   lowerFunctionName.begin(),
                   asciiToLower);
    return lookupFunctionExact(functions, lowerFunctionName);
}

double localMedian(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    const auto middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if ((values.size() & 1U) != 0U) {
        return values[middle];
    }
    const auto lowerMiddle =
        *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return 0.5 * (lowerMiddle + values[middle]);
}

double infValue()
{
    return localInf;
}
double nanValue()
{
    return localNan;
}
double piValue()
{
    return localPi;
}
double randUniform()
{
    return GridRandom::randNumber(GridRandom::DistributionType::UNIFORM);
}
double randNormal()
{
    return GridRandom::randNumber(GridRandom::DistributionType::NORMAL);
}
double randExponential()
{
    return GridRandom::randNumber(GridRandom::DistributionType::EXPONENTIAL);
}
double randLognormal()
{
    return GridRandom::randNumber(GridRandom::DistributionType::LOGNORMAL);
}

double signValue(double val)
{
    if (val > 0.0) {
        return 1.0;
    }
    if (val < 0.0) {
        return -1.0;
    }
    return 0.0;
}
double identityValue(double val)
{
    return val;
}
double decimalValue(double val)
{
    double integerPart;
    return std::modf(val, &integerPart);
}
double randExpSingle(double val)
{
    return GridRandom::randNumber(GridRandom::DistributionType::EXPONENTIAL, val, val);
}

double pow10Value(double val)
{
    return std::pow(10.0, val);
}
double inversePow10Value(double val)
{
    return std::pow(10.0, val);
}
double inversePow2Value(double val)
{
    return std::pow(2.0, val);
}
double absIdentity(double val)
{
    return val;
}
double ceilIdentity(double val)
{
    return val;
}
double floorIdentity(double val)
{
    return val;
}
double roundIdentity(double val)
{
    return val;
}
double truncIdentity(double val)
{
    return val;
}
double sqrtInverse(double val)
{
    return val * val;
}
double cbrtInverse(double val)
{
    return val * val * val;
}
double negSinValue(double val)
{
    return -std::sin(val);
}
double tanDerivative(double val)
{
    const auto cosValue = std::cos(val);
    return 1.0 / (cosValue * cosValue);
}
double tanhDerivative(double val)
{
    const auto tanhValue = std::tanh(val);
    return 1.0 - (tanhValue * tanhValue);
}
double absDerivative(double val)
{
    return (val >= 0.0) ? 1.0 : -1.0;
}
double asinDerivative(double val)
{
    return 1.0 / std::sqrt(1.0 - (val * val));
}
double acosDerivative(double val)
{
    return -1.0 / std::sqrt(1.0 - (val * val));
}
double atanDerivative(double val)
{
    return 1.0 / (1.0 + (val * val));
}
double sqrtDerivative(double val)
{
    return 0.5 / std::sqrt(val);
}
double cbrtDerivative(double val)
{
    const auto root = std::cbrt(val);
    return 1.0 / (3.0 * root * root);
}
double pow2Derivative(double val)
{
    return std::exp2(val) * log2val;
}
double pow10Derivative(double val)
{
    return std::pow(10.0, val) * log10val;
}
double logDerivative(double val)
{
    return 1.0 / val;
}
double log10Derivative(double val)
{
    return 1.0 / (val * log10val);
}
double log2Derivative(double val)
{
    return 1.0 / (val * log2val);
}
double unityDerivative(double /*val*/)
{
    return 1.0;
}
double clampValue(double val, double valLow, double valHigh)
{
    return std::clamp(val, valLow, valHigh);
}
double max3(double val1, double val2, double val3)
{
    return (val1 > val2) ? std::max(val1, val3) : std::max(val2, val3);
}
double min3(double val1, double val2, double val3)
{
    return (val1 > val2) ? std::min(val2, val3) : std::min(val1, val3);
}
double median3(double val1, double val2, double val3)
{
    return std::max(std::min(val1, val2), std::min(std::max(val1, val2), val3));
}

double randUniformRange(double val1, double val2)
{
    return GridRandom::randNumber(GridRandom::DistributionType::UNIFORM, val1, val2);
}
double randNormalRange(double val1, double val2)
{
    return GridRandom::randNumber(GridRandom::DistributionType::NORMAL, val1, val2);
}
double randExponentialRange(double val1, double val2)
{
    return GridRandom::randNumber(GridRandom::DistributionType::EXPONENTIAL, val1, val2);
}
double randLognormalRange(double val1, double val2)
{
    return GridRandom::randNumber(GridRandom::DistributionType::LOGNORMAL, val1, val2);
}
double randIntegerRange(double val1, double val2)
{
    return GridRandom::randNumber(GridRandom::DistributionType::UNIFORM_INT, val1, val2);
}
double randExtremeValueRange(double val1, double val2)
{
    return GridRandom::randNumber(GridRandom::DistributionType::EXTREME_VALUE, val1, val2);
}
double randGammaRange(double val1, double val2)
{
    return GridRandom::randNumber(GridRandom::DistributionType::GAMMA, val1, val2);
}
double addValue(double val1, double val2)
{
    return val1 + val2;
}
double subtractValue(double val1, double val2)
{
    return val1 - val2;
}
double divideValue(double val1, double val2)
{
    return val1 / val2;
}
double multiplyValue(double val1, double val2)
{
    return val1 * val2;
}
double max2(double val1, double val2)
{
    return std::max(val1, val2);
}
double min2(double val1, double val2)
{
    return std::min(val1, val2);
}
double hypot2Value(double val1, double val2)
{
    return std::hypot(val1, val2);
}
double hypot3Value(double val1, double val2, double val3)
{
    return std::hypot(val1, val2, val3);
}
double sum3(double val1, double val2, double val3)
{
    return val1 + val2 + val3;
}
double product3(double val1, double val2, double val3)
{
    return val1 * val2 * val3;
}

double sumArray(const std::vector<double>& ar1)
{
    return sum(ar1);
}
double absMaxArray(const std::vector<double>& ar1)
{
    return absMax(ar1);
}
double maxArray(const std::vector<double>& ar1)
{
    return *std::max_element(ar1.cbegin(), ar1.cend());
}
double minArray(const std::vector<double>& ar1)
{
    return *std::min_element(ar1.cbegin(), ar1.cend());
}
double absMinArray(const std::vector<double>& ar1)
{
    return std::abs(*std::min_element(ar1.begin(), ar1.end(), [](double first, double second) {
        return std::abs(first) < std::abs(second);
    }));
}
double productArray(const std::vector<double>& ar1)
{
    return std::accumulate(ar1.begin(), ar1.end(), 1.0, std::multiplies<double>{});
}
double avgArray(const std::vector<double>& ar1)
{
    return sum(ar1) / static_cast<double>(ar1.size());
}
double stdevArray(const std::vector<double>& ar1)
{
    return stdev(ar1);
}
double medianArray(const std::vector<double>& ar1)
{
    return localMedian(ar1);
}
double vectorProductArray(const std::vector<double>& ar1, const std::vector<double>& ar2)
{
    return mult_sum(ar1, ar2);
}

template<class Fn, std::size_t N>
constexpr auto makeFunctionTable(const FunctionEntry<Fn> (&entries)[N])
{
    return std::to_array(entries);
}
}  // namespace

static constexpr auto funcList0 = makeFunctionTable<function0_t>({
    {"inf", infValue},
    {"nan", nanValue},
    {"pi", piValue},
    {"rand", randUniform},
    {"randn", randNormal},
    {"randexp", randExponential},
    {"randlogn", randLognormal},
});

// Captureless wrappers keep the tables constant-initialized across standard libraries,
// including MSVC math intrinsics when parsed by Clang.
static constexpr auto funcList1 = makeFunctionTable<function1_t>({
    {"sin", [](double value) { return std::sin(value); }},
    {"cos", [](double value) { return std::cos(value); }},
    {"tan", [](double value) { return std::tan(value); }},
    {"sinh", [](double value) { return std::sinh(value); }},
    {"cosh", [](double value) { return std::cosh(value); }},
    {"tanh", [](double value) { return std::tanh(value); }},
    {"abs", [](double value) { return std::abs(value); }},
    {"sign", signValue},
    {"asin", [](double value) { return std::asin(value); }},
    {"acos", [](double value) { return std::acos(value); }},
    {"atan", [](double value) { return std::atan(value); }},
    {"sqrt", [](double value) { return std::sqrt(value); }},
    {"cbrt", [](double value) { return std::cbrt(value); }},
    {"pow2", [](double value) { return std::exp2(value); }},
    {"pow10", pow10Value},
    {"log", [](double value) { return std::log(value); }},
    {"ln", [](double value) { return std::log(value); }},
    {"log10", [](double value) { return std::log10(value); }},
    {"log2", [](double value) { return std::log2(value); }},
    {"exp", [](double value) { return std::exp(value); }},
    {"exp2", [](double value) { return std::exp2(value); }},
    {"ceil", [](double value) { return std::ceil(value); }},
    {"floor", [](double value) { return std::floor(value); }},
    {"round", [](double value) { return std::round(value); }},
    {"trunc", [](double value) { return std::trunc(value); }},
    {"erf", [](double value) { return std::erf(value); }},
    {"erfc", [](double value) { return std::erfc(value); }},
    {"none", identityValue},
    {"dec", decimalValue},
    {"", identityValue},
});

static constexpr auto invFuncList1 = makeFunctionTable<function1_t>({
    {"sin", [](double value) { return std::asin(value); }},
    {"cos", [](double value) { return std::acos(value); }},
    {"tan", [](double value) { return std::atan(value); }},
    {"sinh", [](double value) { return std::asinh(value); }},
    {"cosh", [](double value) { return std::acosh(value); }},
    {"tanh", [](double value) { return std::atanh(value); }},
    {"abs", absIdentity},
    {"sign", signValue},
    {"asin", [](double value) { return std::sin(value); }},
    {"acos", [](double value) { return std::cos(value); }},
    {"atan", [](double value) { return std::tan(value); }},
    {"sqrt", sqrtInverse},
    {"cbrt", cbrtInverse},
    {"pow2", [](double value) { return std::log2(value); }},
    {"pow10", [](double value) { return std::log10(value); }},
    {"log", [](double value) { return std::exp(value); }},
    {"ln", [](double value) { return std::exp(value); }},
    {"log10", inversePow10Value},
    {"log2", inversePow2Value},
    {"exp", [](double value) { return std::log(value); }},
    {"exp2", [](double value) { return std::log2(value); }},
    {"ceil", ceilIdentity},
    {"floor", floorIdentity},
    {"round", roundIdentity},
    {"trunc", truncIdentity},
    {"none", identityValue},
    {"dec", identityValue},
    {"", identityValue},
    {"randexp", randExpSingle},
});

static constexpr auto derivFuncList1 = makeFunctionTable<function1_t>({
    {"sin", [](double value) { return std::cos(value); }},
    {"cos", negSinValue},
    {"tan", tanDerivative},
    {"sinh", [](double value) { return std::cosh(value); }},
    {"cosh", [](double value) { return std::sinh(value); }},
    {"tanh", tanhDerivative},
    {"abs", absDerivative},
    {"asin", asinDerivative},
    {"acos", acosDerivative},
    {"atan", atanDerivative},
    {"sqrt", sqrtDerivative},
    {"cbrt", cbrtDerivative},
    {"pow2", pow2Derivative},
    {"pow10", pow10Derivative},
    {"log", logDerivative},
    {"ln", logDerivative},
    {"log10", log10Derivative},
    {"log2", log2Derivative},
    {"exp", [](double value) { return std::exp(value); }},
    {"exp2", pow2Derivative},
    {"none", unityDerivative},
    {"", unityDerivative},
});

static constexpr auto funcList2 = makeFunctionTable<function2_t>({
    {"atan2", static_cast<function2_t>(std::atan2)},
    {"pow", static_cast<function2_t>(std::pow)},
    {"+", addValue},
    {"-", subtractValue},
    {"/", divideValue},
    {"*", multiplyValue},
    {"^", static_cast<function2_t>(std::pow)},
    {"%", static_cast<function2_t>(std::fmod)},
    {"plus", addValue},
    {"minus", subtractValue},
    {"div", divideValue},
    {"mult", multiplyValue},
    {"product", multiplyValue},
    {"add", addValue},
    {"subtract", subtractValue},
    {"max", max2},
    {"min", min2},
    {"mod", static_cast<function2_t>(std::fmod)},
    {"hypot", hypot2Value},
    {"mag", hypot2Value},
    {"rand", randUniformRange},
    {"randn", randNormalRange},
    {"randexp", randExponentialRange},
    {"randlogn", randLognormalRange},
    {"randint", randIntegerRange},
    {"randexval", randExtremeValueRange},
    {"randgamma", randGammaRange},
});

static constexpr auto funcList3 = makeFunctionTable<function3_t>({
    {"clamp", clampValue},
    {"max", max3},
    {"min", min3},
    {"hypot", hypot3Value},
    {"mag", hypot3Value},
    {"sum", sum3},
    {"product", product3},
    {"median", median3},
});

static constexpr auto arrFuncList1 = makeFunctionTable<vector_function1_t>({
    {"sum", sumArray},
    {"absmax", absMaxArray},
    {"max", maxArray},
    {"min", minArray},
    {"absmin", absMinArray},
    {"product", productArray},
    {"avg", avgArray},
    {"stdev", stdevArray},
    {"median", medianArray},
});

static constexpr auto arrFuncList2 = makeFunctionTable<vector_function2_t>({
    {"vecprod", vectorProductArray},
});

bool isFunctionName(std::string_view functionName, FunctionType ftype)
{
    if (((ftype == FunctionType::ALL) || (ftype == FunctionType::ARG)) &&
        containsFunction(funcList1, functionName)) {
        return true;
    }
    if (((ftype == FunctionType::ALL) || (ftype == FunctionType::ARG2)) &&
        containsFunction(funcList2, functionName)) {
        return true;
    }
    if (((ftype == FunctionType::ALL) || (ftype == FunctionType::ARG3)) &&
        containsFunction(funcList3, functionName)) {
        return true;
    }
    if (((ftype == FunctionType::ALL) || (ftype == FunctionType::NO_ARGS)) &&
        containsFunction(funcList0, functionName)) {
        return true;
    }
    if (((ftype == FunctionType::ALL) || (ftype == FunctionType::VECT_ARG)) &&
        containsFunction(arrFuncList1, functionName)) {
        return true;
    }
    if (((ftype == FunctionType::ALL) || (ftype == FunctionType::VECT_ARG2)) &&
        containsFunction(arrFuncList2, functionName)) {
        return true;
    }
    return false;
}

double evalFunction(std::string_view functionName)
{
    const auto function = lookupFunction(funcList0, functionName);
    return (function != nullptr) ? function() : localNan;
}

double evalFunction(std::string_view functionName, double val)
{
    const auto function = lookupFunction(funcList1, functionName);
    return (function != nullptr) ? function(val) : localNan;
}

double evalFunction(std::string_view functionName, double val1, double val2)
{
    const auto function = lookupFunction(funcList2, functionName);
    return (function != nullptr) ? function(val1, val2) : localNan;
}

double evalFunction(std::string_view functionName, double val1, double val2, double val3)
{
    const auto function = lookupFunction(funcList3, functionName);
    return (function != nullptr) ? function(val1, val2, val3) : localNan;
}

double evalFunction(std::string_view functionName, const std::vector<double>& arr)
{
    const auto function = lookupFunction(arrFuncList1, functionName);
    return (function != nullptr) ? function(arr) : localNan;
}

double evalFunction(std::string_view functionName,
                    const std::vector<double>& arr1,
                    const std::vector<double>& arr2)
{
    const auto function = lookupFunction(arrFuncList2, functionName);
    return (function != nullptr) ? function(arr1, arr2) : localNan;
}

function0_t get0ArgFunction(std::string_view functionName)
{
    return lookupFunction(funcList0, functionName);
}

bool isPureFunction(std::string_view functionName)
{
    std::string lowerName(functionName);
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), asciiToLower);
    return isFunctionName(lowerName) && !lowerName.starts_with("rand");
}

function1_t get1ArgFunction(std::string_view functionName)
{
    return lookupFunction(funcList1, functionName);
}

function1_t getInverse1ArgFunction(std::string_view functionName)
{
    return lookupFunction(invFuncList1, functionName);
}

function1_t getDerivative1ArgFunction(std::string_view functionName)
{
    return lookupFunction(derivFuncList1, functionName);
}

function2_t get2ArgFunction(std::string_view functionName)
{
    return lookupFunction(funcList2, functionName);
}

function3_t get3ArgFunction(std::string_view functionName)
{
    return lookupFunction(funcList3, functionName);
}

vector_function1_t getArrayFunction(std::string_view functionName)
{
    return lookupFunction(arrFuncList1, functionName);
}

vector_function2_t get2ArrayFunction(std::string_view functionName)
{
    return lookupFunction(arrFuncList2, functionName);
}
