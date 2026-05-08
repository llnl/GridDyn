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
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

static constexpr double local_pi = 3.141592653589793;
static const double local_nan = std::nan("0");
static constexpr double local_inf = 1e48;
static const double log2val = log(2.0);
static const double log10val = log(10.0);

using gmlc::utilities::absMax;
using gmlc::utilities::absMin;
using gmlc::utilities::mult_sum;
using gmlc::utilities::product;
using gmlc::utilities::stdev;
using gmlc::utilities::sum;
using utilities::gridRandom;

namespace {
double localMedian(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::stable_sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    if ((values.size() & 1U) != 0U) {
        return values[middle];
    }
    return 0.5 * (values[middle - 1] + values[middle]);
}
}  // namespace

static const std::map<std::string, std::function<double()>> FuncList0{
    std::make_pair("inf", []() { return local_inf; }),
    std::make_pair("nan", []() { return local_nan; }),
    std::make_pair("pi", []() { return local_pi; }),
    std::make_pair("rand",
                   []() { return gridRandom::randNumber(gridRandom::dist_type_t::uniform); }),
    std::make_pair("randn",
                   []() { return gridRandom::randNumber(gridRandom::dist_type_t::normal); }),
    std::make_pair("randexp",
                   []() { return gridRandom::randNumber(gridRandom::dist_type_t::exponential); }),
    std::make_pair("randlogn",
                   []() { return gridRandom::randNumber(gridRandom::dist_type_t::lognormal); }),
};

static const std::map<std::string, std::function<double(double)>> FuncList1{
    std::make_pair("sin", [](double val) { return sin(val); }),
    std::make_pair("cos", [](double val) { return cos(val); }),
    std::make_pair("tan", [](double val) { return tan(val); }),
    std::make_pair("sinh", [](double val) { return sinh(val); }),
    std::make_pair("cosh", [](double val) { return cosh(val); }),
    std::make_pair("tanh", [](double val) { return tanh(val); }),
    std::make_pair("abs", [](double val) { return std::abs(val); }),
    std::make_pair("sign",
                   [](double val) { return (val > 0.0) ? (1.0) : ((val < 0.0) ? -1.0 : 0.0); }),
    std::make_pair("asin", [](double val) { return asin(val); }),
    std::make_pair("acos", [](double val) { return acos(val); }),
    std::make_pair("atan", [](double val) { return atan(val); }),
    std::make_pair("sqrt", [](double val) { return sqrt(val); }),
    std::make_pair("cbrt", [](double val) { return cbrt(val); }),
    std::make_pair("pow2", [](double val) { return exp2(val); }),
    std::make_pair("pow10", [](double val) { return pow(10.0, val); }),
    std::make_pair("log", [](double val) { return log(val); }),
    std::make_pair("ln", [](double val) { return log(val); }),
    std::make_pair("log10", [](double val) { return log10(val); }),
    std::make_pair("log2", [](double val) { return log2(val); }),
    std::make_pair("exp", [](double val) { return exp(val); }),
    std::make_pair("exp2", [](double val) { return exp2(val); }),
    std::make_pair("ceil", [](double val) { return ceil(val); }),
    std::make_pair("floor", [](double val) { return floor(val); }),
    std::make_pair("round", [](double val) { return round(val); }),
    std::make_pair("trunc", [](double val) { return trunc(val); }),
    std::make_pair("erf", [](double val) { return erf(val); }),
    std::make_pair("erfc", [](double val) { return erfc(val); }),
    std::make_pair("none", [](double val) { return val; }),
    std::make_pair("dec",
                   [](double val) {
                       double iptr;
                       return std::modf(val, &iptr);
                   }),
    std::make_pair("", [](double val) { return val; }),
    std::make_pair("randexp", [](double val) {
        return gridRandom::randNumber(gridRandom::dist_type_t::exponential, val, val);
    })};

static const std::map<std::string, std::function<double(double)>> invFuncList1{
    std::make_pair("sin", [](double val) { return asin(val); }),
    std::make_pair("cos", [](double val) { return acos(val); }),
    std::make_pair("tan", [](double val) { return atan(val); }),
    std::make_pair("sinh", [](double val) { return asinh(val); }),
    std::make_pair("cosh", [](double val) { return acosh(val); }),
    std::make_pair("tanh", [](double val) { return atanh(val); }),
    std::make_pair("abs", [](double val) { return val; }),
    std::make_pair("sign",
                   [](double val) { return (val > 0.0) ? (1.0) : ((val < 0.0) ? -1.0 : 0.0); }),
    std::make_pair("asin", [](double val) { return sin(val); }),
    std::make_pair("acos", [](double val) { return cos(val); }),
    std::make_pair("atan", [](double val) { return tan(val); }),
    std::make_pair("sqrt", [](double val) { return val * val; }),
    std::make_pair("cbrt", [](double val) { return val * val * val; }),
    std::make_pair("pow2", [](double val) { return log2(val); }),
    std::make_pair("pow10", [](double val) { return log10(val); }),
    std::make_pair("log", [](double val) { return exp(val); }),
    std::make_pair("ln", [](double val) { return exp(val); }),
    std::make_pair("log10", [](double val) { return pow(10.0, val); }),
    std::make_pair("log2", [](double val) { return pow(2.0, val); }),
    std::make_pair("exp", [](double val) { return log(val); }),
    std::make_pair("exp2", [](double val) { return log2(val); }),
    std::make_pair("ceil", [](double val) { return val; }),
    std::make_pair("floor", [](double val) { return val; }),
    std::make_pair("round", [](double val) { return val; }),
    std::make_pair("trunc", [](double val) { return val; }),
    std::make_pair("none", [](double val) { return val; }),
    std::make_pair("dec", [](double val) { return val; }),
    std::make_pair("", [](double val) { return val; }),
};

static const std::map<std::string, std::function<double(double)>> derivFuncList1{
    std::make_pair("sin", [](double val) { return cos(val); }),
    std::make_pair("cos", [](double val) { return -sin(val); }),
    std::make_pair("tan", [](double val) { return pow(1.0 / cos(val), 2.0); }),
    std::make_pair("sinh", [](double val) { return cosh(val); }),
    std::make_pair("cosh", [](double val) { return sinh(val); }),
    std::make_pair("tanh", [](double val) { return 1.0 - pow(tanh(val), 2.0); }),
    std::make_pair("abs", [](double val) { return (val >= 0.0) ? 1.0 : -1.0; }),
    std::make_pair("asin", [](double val) { return (1.0 / sqrt(1.0 - val * val)); }),
    std::make_pair("acos", [](double val) { return (-1.0 / sqrt(1.0 - val * val)); }),
    std::make_pair("atan", [](double val) { return (1.0 / (1.0 + val * val)); }),
    std::make_pair("sqrt", [](double val) { return 0.5 / sqrt(val); }),
    std::make_pair("cbrt", [](double val) { return 1.0 / 3.0 * pow(val, -2.0 / 3.0); }),
    std::make_pair("pow2", [](double val) { return exp2(val) * log2val; }),
    std::make_pair("pow10", [](double val) { return pow(10.0, val) * log10val; }),
    std::make_pair("log", [](double val) { return 1.0 / val; }),
    std::make_pair("ln", [](double val) { return 1.0 / val; }),
    std::make_pair("log10", [](double val) { return (1.0 / val) * log10val; }),
    std::make_pair("log2", [](double val) { return (1.0 / val) * log2val; }),
    std::make_pair("exp", [](double val) { return exp(val); }),
    std::make_pair("exp2", [](double val) { return exp2(val) * log2val; }),
    std::make_pair("none", [](double /*val*/) { return 1.0; }),
    std::make_pair("", [](double /*val*/) { return 1.0; }),
};

static const std::map<std::string, std::function<double(double, double)>> FuncList2{
    std::make_pair("atan2", [](double val1, double val2) { return atan2(val1, val2); }),
    std::make_pair("pow", [](double val1, double val2) { return pow(val1, val2); }),
    std::make_pair("+", [](double val1, double val2) { return val1 + val2; }),
    std::make_pair("-", [](double val1, double val2) { return val1 - val2; }),
    std::make_pair("/", [](double val1, double val2) { return val1 / val2; }),
    std::make_pair("*", [](double val1, double val2) { return val1 * val2; }),
    std::make_pair("^", [](double val1, double val2) { return pow(val1, val2); }),
    std::make_pair("%", [](double val1, double val2) { return fmod(val1, val2); }),
    std::make_pair("plus", [](double val1, double val2) { return val1 + val2; }),
    std::make_pair("minus", [](double val1, double val2) { return val1 - val2; }),
    std::make_pair("div", [](double val1, double val2) { return val1 / val2; }),
    std::make_pair("mult", [](double val1, double val2) { return val1 * val2; }),
    std::make_pair("product", [](double val1, double val2) { return val1 * val2; }),
    std::make_pair("add", [](double val1, double val2) { return val1 + val2; }),
    std::make_pair("subtract", [](double val1, double val2) { return val1 - val2; }),
    std::make_pair("max", [](double val1, double val2) { return std::max(val1, val2); }),
    std::make_pair("min", [](double val1, double val2) { return std::min(val1, val2); }),
    std::make_pair("mod", [](double val1, double val2) { return fmod(val1, val2); }),
    std::make_pair("hypot", [](double val1, double val2) { return hypot(val1, val2); }),
    std::make_pair("mag", [](double val1, double val2) { return hypot(val1, val2); }),
    std::make_pair("rand",
                   [](double val1, double val2) {
                       return gridRandom::randNumber(gridRandom::dist_type_t::uniform, val1, val2);
                   }),
    std::make_pair("randn",
                   [](double val1, double val2) {
                       return gridRandom::randNumber(gridRandom::dist_type_t::normal, val1, val2);
                   }),
    std::make_pair("randexp",
                   [](double val1, double val2) {
                       return gridRandom::randNumber(gridRandom::dist_type_t::exponential,
                                                     val1,
                                                     val2);
                   }),
    std::make_pair("randlogn",
                   [](double val1, double val2) {
                       return gridRandom::randNumber(gridRandom::dist_type_t::lognormal,
                                                     val1,
                                                     val2);
                   }),
    std::make_pair("randint",
                   [](double val1, double val2) {
                       return gridRandom::randNumber(gridRandom::dist_type_t::uniform_int,
                                                     val1,
                                                     val2);
                   }),
    std::make_pair("randexval",
                   [](double val1, double val2) {
                       return gridRandom::randNumber(gridRandom::dist_type_t::extreme_value,
                                                     val1,
                                                     val2);
                   }),
    std::make_pair("randgamma",
                   [](double val1, double val2) {
                       return gridRandom::randNumber(gridRandom::dist_type_t::gamma, val1, val2);
                   }),
};

static const std::map<std::string, std::function<double(double, double, double)>> FuncList3{
    std::make_pair("clamp",
                   [](double val, double valLow, double valHigh) {
                       return std::clamp(val, valLow, valHigh);
                   }),
    std::make_pair("max",
                   [](double val1, double val2, double val3) {
                       return (val1 > val2) ? std::max(val1, val3) : std::max(val2, val3);
                   }),
    std::make_pair("min",
                   [](double val1, double val2, double val3) {
                       return (val1 > val2) ? std::min(val2, val3) : std::min(val1, val3);
                   }),
    std::make_pair("hypot",
                   [](double val1, double val2, double val3) {
                       return std::hypot(val1, val2, val3);
                   }),
    std::make_pair("mag",
                   [](double val1, double val2, double val3) {
                       return std::hypot(val1, val2, val3);
                   }),
    std::make_pair("sum", [](double val1, double val2, double val3) { return val1 + val2 + val3; }),
    std::make_pair("product",
                   [](double val1, double val2, double val3) { return val1 * val2 * val3; }),
    std::make_pair("median",
                   [](double val1, double val2, double val3) {
                       return std::max(std::min(val1, val2), std::min(std::max(val1, val2), val3));
                   }),
};

static std::map<std::string, std::function<double(const std::vector<double>&)>> ArrFuncList1{
    std::make_pair("sum", [](const std::vector<double>& ar1) { return sum(ar1); }),
    std::make_pair("absmax", [](const std::vector<double>& ar1) { return absMax(ar1); }),
    std::make_pair("max",
                   [](const std::vector<double>& ar1) {
                       return *std::max_element(ar1.cbegin(), ar1.cend());
                   }),
    std::make_pair("min",
                   [](const std::vector<double>& ar1) {
                       return *std::min_element(ar1.cbegin(), ar1.cend());
                   }),
    std::make_pair("absmin", [](const std::vector<double>& ar1) { return absMin(ar1); }),
    std::make_pair("product", [](const std::vector<double>& ar1) { return product(ar1); }),
    std::make_pair("avg",
                   [](const std::vector<double>& ar1) {
                       return (sum(ar1) / static_cast<double>(ar1.size()));
                   }),
    std::make_pair("stdev", [](const std::vector<double>& ar1) { return stdev(ar1); }),
    std::make_pair("median", [](const std::vector<double>& ar1) { return localMedian(ar1); }),
};

static const std::map<std::string,
                      std::function<double(const std::vector<double>&, const std::vector<double>&)>>
    ArrFuncList2{
        std::make_pair("vecprod",
                       [](const std::vector<double>& ar1, const std::vector<double>& ar2) {
                           return mult_sum(ar1, ar2);
                       }),
    };

bool isFunctionName(const std::string& functionName, function_type ftype)
{
    if ((ftype == function_type::all) || (ftype == function_type::arg)) {
        auto ifind = FuncList1.find(functionName);
        if (ifind != FuncList1.end()) {
            return true;
        }
    }
    if ((ftype == function_type::all) || (ftype == function_type::arg2)) {
        auto ifind2 = FuncList2.find(functionName);
        if (ifind2 != FuncList2.end()) {
            return true;
        }
    }
    if ((ftype == function_type::all) || (ftype == function_type::arg3)) {
        auto ifind3 = FuncList3.find(functionName);
        if (ifind3 != FuncList3.end()) {
            return true;
        }
    }
    if ((ftype == function_type::all) || (ftype == function_type::no_args)) {
        auto ifind = FuncList0.find(functionName);
        if (ifind != FuncList0.end()) {
            return true;
        }
    }
    if ((ftype == function_type::all) || (ftype == function_type::vect_arg)) {
        auto ifind3 = ArrFuncList1.find(functionName);
        if (ifind3 != ArrFuncList1.end()) {
            return true;
        }
    }
    if ((ftype == function_type::all) || (ftype == function_type::vect_arg2)) {
        auto ifind4 = ArrFuncList2.find(functionName);
        if (ifind4 != ArrFuncList2.end()) {
            return true;
        }
    }
    return false;
}

double evalFunction(const std::string& functionName)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    auto fret = FuncList0.find(temp);
    return (fret != FuncList0.end()) ? fret->second() : local_nan;
}

double evalFunction(const std::string& functionName, double val)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    auto fret = FuncList1.find(temp);
    return (fret != FuncList1.end()) ? fret->second(val) : local_nan;
}

double evalFunction(const std::string& functionName, double val1, double val2)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    auto fret = FuncList2.find(temp);
    return (fret != FuncList2.end()) ? fret->second(val1, val2) : local_nan;
}

double evalFunction(const std::string& functionName, double val1, double val2, double val3)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    auto fret = FuncList3.find(temp);
    return (fret != FuncList3.end()) ? fret->second(val1, val2, val3) : local_nan;
}

double evalFunction(const std::string& functionName, const std::vector<double>& arr)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);

    auto fret = ArrFuncList1.find(temp);
    return (fret != ArrFuncList1.end()) ? fret->second(arr) : local_nan;
}

double evalFunction(const std::string& functionName,
                    const std::vector<double>& arr1,
                    const std::vector<double>& arr2)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);

    auto fret = ArrFuncList2.find(temp);
    return (fret != ArrFuncList2.end()) ? fret->second(arr1, arr2) : local_nan;
}

std::function<double()> get0ArgFunction(const std::string& functionName)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);

    auto fret = FuncList0.find(temp);
    return (fret != FuncList0.end()) ? fret->second : nullptr;
}

std::function<double(double)> get1ArgFunction(const std::string& functionName)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    return mapFind(FuncList1, temp, std::function<double(double)>(nullptr));
}

std::function<double(double, double)> get2ArgFunction(const std::string& functionName)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    return mapFind(FuncList2, temp, std::function<double(double, double)>(nullptr));
}

std::function<double(double, double, double)> get3ArgFunction(const std::string& functionName)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    return mapFind(FuncList3, temp, std::function<double(double, double, double)>(nullptr));
}

std::function<double(const std::vector<double>&)> getArrayFunction(const std::string& functionName)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    return mapFind(ArrFuncList1, temp, std::function<double(const std::vector<double>&)>(nullptr));
}

std::function<double(const std::vector<double>&, const std::vector<double>&)>
    get2ArrayFunction(const std::string& functionName)
{
    std::string temp = gmlc::utilities::convertToLowerCase(functionName);
    return mapFind(ArrFuncList2,
                   temp,
                   std::function<double(const std::vector<double>&, const std::vector<double>&)>(
                       nullptr));
}
