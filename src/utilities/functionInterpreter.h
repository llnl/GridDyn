/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

/** @brief enumeration of the different function types*/
enum class FunctionType {
    ALL,  //!< all possible function types
    NO_ARGS,  //!< functions with no arguments
    ARG,  //!< function with 1 argument
    ARG2,  //!< functions with 2 arguments
    ARG3,  // 1< functions with 3 arguments
    VECT_ARG,  //!< function with a vector arguments
    VECT_ARG2  //!< functions with 2 vector arguments
};

using function0_t = double (*)();
using function1_t = double (*)(double);
using function2_t = double (*)(double, double);
using function3_t = double (*)(double, double, double);
using vector_function1_t = double (*)(const std::vector<double>&);
using vector_function2_t = double (*)(const std::vector<double>&, const std::vector<double>&);

/** @brief evaluate a function named by a string
@param[in] functionName the function name to evaluate
@return the evaluated function or nan;
*/
double evalFunction(std::string_view functionName);

/** @brief evaluate a function named by a string with 1 argument
@param[in] functionName the function name to evaluate
@param[in] val the argument value of the function
@return the evaluated function or nan;
*/
double evalFunction(std::string_view functionName, double val);

/** @brief evaluate a function named by a string with 2 arguments
@param[in] functionName the function name to evaluate
@param[in] val1 the first argument value of the function
@param[in] val2 the second argument value of the function
@return the evaluated function or nan;
*/
double evalFunction(std::string_view functionName, double val1, double val2);

/** @brief evaluate a function named by a string with 3 arguments
@param[in] functionName the function name to evaluate
@param[in] val1 the first argument value of the function
@param[in] val2 the second argument value of the function
@param[in] val3 the third argument value of the function
@return the evaluated function or nan;
*/
double evalFunction(std::string_view functionName, double val1, double val2, double val3);

/** @brief evaluate a function named by a string with 1 array argument
@param[in] functionName the function name to evaluate
@param[in] arr the array argument for evaluation
@return the evaluated function or nan;
*/
double evalFunction(std::string_view functionName, const std::vector<double>& arr);

/** @brief evaluate a function named by a string with 2 array arguments
@param[in] functionName the function name to evaluate
@param[in] arr1 the first array argument for evaluation
@param[in] arr2 the second array argument for evaluation
@return the evaluated function or nan;
*/
double evalFunction(std::string_view functionName,
                    const std::vector<double>& arr1,
                    const std::vector<double>& arr2);

/** @brief check if a string represents a valid function
@param[in] functionName the function name to tests
@param[in] ftype the class of functions to check
@return true if the string is a function name false otherwise
*/
bool isFunctionName(std::string_view functionName, FunctionType ftype = FunctionType::ALL);

/** @brief find a no argument function and return the corresponding lambda function
@param[in] functionName the function name
@return a std::Function with the appropriate function
*/
function0_t get0ArgFunction(std::string_view functionName);

/** @brief find a single argument function and return the corresponding lambda function
@param[in] functionName the function name
@return a std::Function with the appropriate function
*/
function1_t get1ArgFunction(std::string_view functionName);

/** @brief find the inverse of a single argument function when available
@param[in] functionName the function name
@return the inverse function or nullptr if not available
*/
function1_t getInverse1ArgFunction(std::string_view functionName);

/** @brief find the derivative of a single argument function when available
@param[in] functionName the function name
@return the derivative function or nullptr if not available
*/
function1_t getDerivative1ArgFunction(std::string_view functionName);

/** @brief find a two argument function and return the corresponding lambda function
@param[in] functionName the function name
@return a std::Function with the appropriate function
*/
function2_t get2ArgFunction(std::string_view functionName);

/** @brief find a three argument function and return the corresponding lambda function
@param[in] functionName the function name
@return a std::Function with the appropriate function
*/
function3_t get3ArgFunction(std::string_view functionName);

/** @brief find a function with a single array as an argument and return the corresponding lambda
function
@param[in] functionName the function name
@return a std::Function implementing the appropriate function
*/
vector_function1_t getArrayFunction(std::string_view functionName);

/** @brief find a function with a two arrays as arguments and return the corresponding lambda
function
@param[in] functionName the function name
@return a std::Function implementing the appropriate function
*/
vector_function2_t get2ArrayFunction(std::string_view functionName);
