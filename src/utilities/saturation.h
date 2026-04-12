/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */
#pragma once

#include <functional>
#include <string>
namespace utilities {
/** @brief class implementing a saturation model
 *@details 4 mathematical models are available including: quadratic, scaled_quadratic, exponential,
 *and linear
 */
class saturation {
  public:
    /** @brief enumeration of saturation types
     */
    enum class satType_t { none, quadratic, scaled_quadratic, exponential, linear };

  private:
    double s10 = 0.0;  //!< s10 parameter
    double s12 = 0.0;  //!< s12 parameter
    double A = 0.0;  //!< parameter 1 of the saturation
    double B = 0.0;  //!< parameter 2 of the saturation
    std::function<double(double)> satFunc;  //!< the function that calculates the saturated value
    std::function<double(double)> derivFunc;  //!< the derivative of the saturation

    satType_t type;  //!< the type of the saturation
  public:
    /** construction saturation from saturation type
     * @details constructor is converting type
     * @param[in] sT saturation Type
     */
    explicit saturation(satType_t sT = satType_t::scaled_quadratic);
    /** construct from string naming saturation type
     *@param[in] satType a string containing the type of the saturation*/
    explicit saturation(const std::string& satType);
    /** set the S10 and S12 parameter
     *@details sets the parameters of the saturation function previously specified at the point 1.0
     *and 1.2 The values input should correspond to the reduction in values so 0.0 for no saturation
     *@param[in] S1  the value reduction at 1.0
     *@param[in] S2 the value reduction at 1.2
     */
    void setParam(double S1, double S2);
    /** @brief define the saturation function by specifying the reduction at two points
     *@details sets the parameters of the saturation function previously specified at the points V1
     *and V2 The values input should correspond to the reduction in values so 0.0 for no saturation
     *@param[in] V1 the point along the saturation curve that S1 is given
     *@param[in] S1  the value reduction at V1
     *@param[in] V2 the point along the saturation curve from which S2 is given
     *@param[in] S2 the value reduction at V2
     */
    void setParam(double V1, double S1, double V2, double S2);
    /** update the saturation type function by enumeration*/
    void setType(satType_t sT);
    /** update the saturation function by a string*/
    void setType(const std::string& stype);
    /** get the saturation function type by enumeration*/
    satType_t getType() const;
    /** @brief compute the saturation value
     * @param[in] val input value
     * @return the reduction due to the saturation function
     */
    double operator()(double val) const;
    /** @brief compute the saturation value
     * @param[in] val input value
     * @return the reduction due to the saturation function
     */
    double compute(double val) const;
    /** @brief compute the derivative of the saturation with respect to the input value
     * @param[in] val input value
     * @return the derivative of the saturation level with respect to the trustees
     */
    double deriv(double val) const;
    /** @brief compute the inverse value given a saturation level
     *@details values below 0.00001 return 0.5 so there is no numeric instability
     *@param[in] val the saturation level
     *@return the value that would be input to achieve that saturation level
     */
    double inv(double val) const;

  private:
    /** compute the A and B parameters*/
    void computeParam();
    /** load the internal functions of a given saturation function*/
    void loadFunctions();
};

}  // namespace utilities
