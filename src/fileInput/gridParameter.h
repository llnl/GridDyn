/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "units/units.hpp"
#include <string>
#include <vector>
namespace griddyn {
/** @brief helper class for representing a parameter of various types
 * @details data class to extract some parameter information from a string
 */
class gridParameter {
  public:
    std::string field;  //!< the field of the parameter
    std::string strVal;  //!< a string value of the parameter
    std::vector<int> applyIndex;  //!< a vector of indices the parameter should be applied to
    double value = 0.0;  //!< the numerical value of the parameter
    units::unit paramUnits = units::defunit;  //!< the units used for the parameter
    bool valid = false;  //!< indicator if the parameter is valid
    bool stringType = false;  //!< indicator that the parameter is using the string property
    /** @brief constructor*/
    gridParameter();
    /** @brief alternate constructor
    @param[in] str  call the from string constructor
    */
    explicit gridParameter(const std::string& str);
    /** @brief alternate constructor
    @param[in] fld  the field of the parameter
    @param[in] val  the value of the parameter
    */
    gridParameter(std::string fld, double val);
    /** @brief alternate constructor
    @param[in] fld  the field of the parameter
    @param[in] val  the string value of the parameter
    */
    gridParameter(std::string fld, std::string val);
    /** @brief reset the parameter*/
    void reset();
    /** @brief load a gridParameter from a string*
    @param[in] str the string to load from
    @throw invalidParameterValue on failure
    */
    void fromString(const std::string& str);
};

}  // namespace griddyn
