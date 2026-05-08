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

#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "gridRandom.h"
#include "griddyn/griddyn-config.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static constexpr double local_pi = 3.141592653589793;
static const double local_nan = std::nan("0");
static constexpr double local_inf = 1e48;
static constexpr double log2val = 0.69314718055994530942;
static constexpr double log10val = 2.30258509299404568402;

using gmlc::utilities::absMax;
using gmlc::utilities::absMin;
using gmlc::utilities::mult_sum;
using gmlc::utilities::product;
using gmlc::utilities::stdev;
using gmlc::utilities::sum;
using utilities::gridRandom;

namespace {
template<class Fn>
struct functionEntry {
    std::string_view name;
    Fn function;
};

constexpr char asciiToLower(char ch) noexcept
{
    return ((ch >= 'A') && (ch <= 'Z')) ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool isLowerAscii(std::string_view str) noexcept
{
    return std::all_of(str.begin(), str.end(), [](char ch) { return ((ch < 'A') || (ch > 'Z')); });
}

template<class Fn, std::size_t N>
Fn lookupFunctionExact(const std::array<functionEntry<Fn>, N>& functions,
                       std::string_view functionName)
{
    const auto functionMatch =
        std::find_if(functions.begin(), functions.end(), [functionName](const auto& entry) {
            return entry.name == functionName;
        });
    return (functionMatch != functions.end()) ? functionMatch->function : nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<functionEntry<function0_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<functionEntry<function1_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<functionEntry<function2_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<functionEntry<function3_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<functionEntry<vector_function1_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<std::size_t N>
bool containsFunction(const std::array<functionEntry<vector_function2_t>, N>& functions,
                      std::string_view functionName)
{
    return lookupFunction(functions, functionName) != nullptr;
}

template<class Fn, std::size_t N>
Fn lookupFunction(const std::array<functionEntry<Fn>, N>& functions, std::string_view functionName)
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
    return local_inf;
}
double nanValue()
{
    return local_nan;
}
double piValue()
{
    return local_pi;
}
double randUniform()
{
    return gridRandom::randNumber(gridRandom::dist_type_t::uniform);
}
double randNormal()
{
    return gridRandom::randNumber(gridRandom::dist_type_t::normal);
}
double randExponential()
{
    return gridRandom::randNumber(gridRandom::dist_type_t::exponential);
}
double randLognormal()
{
    return gridRandom::randNumber(gridRandom::dist_type_t::lognormal);
}

double signValue(double val)
{
    return (val > 0.0) ? 1.0 : ((val < 0.0) ? -1.0 : 0.0);
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
    return gridRandom::randNumber(gridRandom::dist_type_t::exponential, val, val);
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
    return 1.0 - tanhValue * tanhValue;
}
double absDerivative(double val)
{
    return (val >= 0.0) ? 1.0 : -1.0;
}
double asinDerivative(double val)
{
    return 1.0 / std::sqrt(1.0 - val * val);
}
double acosDerivative(double val)
{
    return -1.0 / std::sqrt(1.0 - val * val);
}
double atanDerivative(double val)
{
    return 1.0 / (1.0 + val * val);
}
double sqrtDerivative(double val)
{
    return 0.5 / std::sqrt(val);
}
double cbrtDerivative(double val)
{
    return (1.0 / 3.0) * std::pow(val, -2.0 / 3.0);
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
    return (1.0 / val) * log10val;
}
double log2Derivative(double val)
{
    return (1.0 / val) * log2val;
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
    return gridRandom::randNumber(gridRandom::dist_type_t::uniform, val1, val2);
}
double randNormalRange(double val1, double val2)
{
    return gridRandom::randNumber(gridRandom::dist_type_t::normal, val1, val2);
}
double randExponentialRange(double val1, double val2)
{
    return gridRandom::randNumber(gridRandom::dist_type_t::exponential, val1, val2);
}
double randLognormalRange(double val1, double val2)
{
    return gridRandom::randNumber(gridRandom::dist_type_t::lognormal, val1, val2);
}
double randIntegerRange(double val1, double val2)
{
    return gridRandom::randNumber(gridRandom::dist_type_t::uniform_int, val1, val2);
}
double randExtremeValueRange(double val1, double val2)
{
    return gridRandom::randNumber(gridRandom::dist_type_t::extreme_value, val1, val2);
}
double randGammaRange(double val1, double val2)
{
    return gridRandom::randNumber(gridRandom::dist_type_t::gamma, val1, val2);
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
    return absMin(ar1);
}
double productArray(const std::vector<double>& ar1)
{
    return product(ar1);
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
constexpr auto makeFunctionTable(const functionEntry<Fn> (&entries)[N])
{
    return std::to_array(entries);
}
}  // namespace

static constexpr auto FuncList0 = makeFunctionTable<function0_t>({
    {"inf", infValue},
    {"nan", nanValue},
    {"pi", piValue},
    {"rand", randUniform},
    {"randn", randNormal},
    {"randexp", randExponential},
    {"randlogn", randLognormal},
});

static constexpr auto FuncList1 = makeFunctionTable<function1_t>({
    {"sin", static_cast<function1_t>(std::sin)},
    {"cos", static_cast<function1_t>(std::cos)},
    {"tan", static_cast<function1_t>(std::tan)},
    {"sinh", static_cast<function1_t>(std::sinh)},
    {"cosh", static_cast<function1_t>(std::cosh)},
    {"tanh", static_cast<function1_t>(std::tanh)},
    {"abs", static_cast<function1_t>(std::abs)},
    {"sign", signValue},
    {"asin", static_cast<function1_t>(std::asin)},
    {"acos", static_cast<function1_t>(std::acos)},
    {"atan", static_cast<function1_t>(std::atan)},
    {"sqrt", static_cast<function1_t>(std::sqrt)},
    {"cbrt", static_cast<function1_t>(std::cbrt)},
    {"pow2", static_cast<function1_t>(std::exp2)},
    {"pow10", pow10Value},
    {"log", static_cast<function1_t>(std::log)},
    {"ln", static_cast<function1_t>(std::log)},
    {"log10", static_cast<function1_t>(std::log10)},
    {"log2", static_cast<function1_t>(std::log2)},
    {"exp", static_cast<function1_t>(std::exp)},
    {"exp2", static_cast<function1_t>(std::exp2)},
    {"ceil", static_cast<function1_t>(std::ceil)},
    {"floor", static_cast<function1_t>(std::floor)},
    {"round", static_cast<function1_t>(std::round)},
    {"trunc", static_cast<function1_t>(std::trunc)},
    {"erf", static_cast<function1_t>(std::erf)},
    {"erfc", static_cast<function1_t>(std::erfc)},
    {"none", identityValue},
    {"dec", decimalValue},
    {"", identityValue},
});

static constexpr auto invFuncList1 = makeFunctionTable<function1_t>({
    {"sin", static_cast<function1_t>(std::asin)},
    {"cos", static_cast<function1_t>(std::acos)},
    {"tan", static_cast<function1_t>(std::atan)},
    {"sinh", static_cast<function1_t>(std::asinh)},
    {"cosh", static_cast<function1_t>(std::acosh)},
    {"tanh", static_cast<function1_t>(std::atanh)},
    {"abs", absIdentity},
    {"sign", signValue},
    {"asin", static_cast<function1_t>(std::sin)},
    {"acos", static_cast<function1_t>(std::cos)},
    {"atan", static_cast<function1_t>(std::tan)},
    {"sqrt", sqrtInverse},
    {"cbrt", cbrtInverse},
    {"pow2", static_cast<function1_t>(std::log2)},
    {"pow10", static_cast<function1_t>(std::log10)},
    {"log", static_cast<function1_t>(std::exp)},
    {"ln", static_cast<function1_t>(std::exp)},
    {"log10", inversePow10Value},
    {"log2", inversePow2Value},
    {"exp", static_cast<function1_t>(std::log)},
    {"exp2", static_cast<function1_t>(std::log2)},
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
    {"sin", static_cast<function1_t>(std::cos)},
    {"cos", negSinValue},
    {"tan", tanDerivative},
    {"sinh", static_cast<function1_t>(std::cosh)},
    {"cosh", static_cast<function1_t>(std::sinh)},
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
    {"exp", static_cast<function1_t>(std::exp)},
    {"exp2", pow2Derivative},
    {"none", unityDerivative},
    {"", unityDerivative},
});

static constexpr auto FuncList2 = makeFunctionTable<function2_t>({
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

static constexpr auto FuncList3 = makeFunctionTable<function3_t>({
    {"clamp", clampValue},
    {"max", max3},
    {"min", min3},
    {"hypot", hypot3Value},
    {"mag", hypot3Value},
    {"sum", sum3},
    {"product", product3},
    {"median", median3},
});

static constexpr auto ArrFuncList1 = makeFunctionTable<vector_function1_t>({
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

static constexpr auto ArrFuncList2 = makeFunctionTable<vector_function2_t>({
    {"vecprod", vectorProductArray},
});

bool isFunctionName(std::string_view functionName, function_type ftype)
{
    if (((ftype == function_type::all) || (ftype == function_type::arg)) &&
        containsFunction(FuncList1, functionName)) {
        return true;
    }
    if (((ftype == function_type::all) || (ftype == function_type::arg2)) &&
        containsFunction(FuncList2, functionName)) {
        return true;
    }
    if (((ftype == function_type::all) || (ftype == function_type::arg3)) &&
        containsFunction(FuncList3, functionName)) {
        return true;
    }
    if (((ftype == function_type::all) || (ftype == function_type::no_args)) &&
        containsFunction(FuncList0, functionName)) {
        return true;
    }
    if (((ftype == function_type::all) || (ftype == function_type::vect_arg)) &&
        containsFunction(ArrFuncList1, functionName)) {
        return true;
    }
    if (((ftype == function_type::all) || (ftype == function_type::vect_arg2)) &&
        containsFunction(ArrFuncList2, functionName)) {
        return true;
    }
    return false;
}

double evalFunction(std::string_view functionName)
{
    const auto function = lookupFunction(FuncList0, functionName);
    return (function != nullptr) ? function() : local_nan;
}

double evalFunction(std::string_view functionName, double val)
{
    const auto function = lookupFunction(FuncList1, functionName);
    return (function != nullptr) ? function(val) : local_nan;
}

double evalFunction(std::string_view functionName, double val1, double val2)
{
    const auto function = lookupFunction(FuncList2, functionName);
    return (function != nullptr) ? function(val1, val2) : local_nan;
}

double evalFunction(std::string_view functionName, double val1, double val2, double val3)
{
    const auto function = lookupFunction(FuncList3, functionName);
    return (function != nullptr) ? function(val1, val2, val3) : local_nan;
}

double evalFunction(std::string_view functionName, const std::vector<double>& arr)
{
    const auto function = lookupFunction(ArrFuncList1, functionName);
    return (function != nullptr) ? function(arr) : local_nan;
}

double evalFunction(std::string_view functionName,
                    const std::vector<double>& arr1,
                    const std::vector<double>& arr2)
{
    const auto function = lookupFunction(ArrFuncList2, functionName);
    return (function != nullptr) ? function(arr1, arr2) : local_nan;
}

function0_t get0ArgFunction(std::string_view functionName)
{
    return lookupFunction(FuncList0, functionName);
}

function1_t get1ArgFunction(std::string_view functionName)
{
    return lookupFunction(FuncList1, functionName);
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
    return lookupFunction(FuncList2, functionName);
}

function3_t get3ArgFunction(std::string_view functionName)
{
    return lookupFunction(FuncList3, functionName);
}

vector_function1_t getArrayFunction(std::string_view functionName)
{
    return lookupFunction(ArrFuncList1, functionName);
}

vector_function2_t get2ArrayFunction(std::string_view functionName)
{
    return lookupFunction(ArrFuncList2, functionName);
}
