/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <functional>
#include <string>
namespace utilities {
/** @brief Class implementing several saturation characteristics.
 *
 * @details Saturation points supplied with setParam(double, double) are the
 * values at inputs 1.0 and 1.2. The cutoff-scaled-quadratic characteristic is
 * the form used by PSS/E synchronous-machine models such as GENROU:
 * \f[
 * S(x)=\begin{cases}
 * 0,&x<A,\\
 * B(x-A)^2/x,&x\ge A.
 * \end{cases}
 * \f]
 * Non-positive saturation points disable the characteristic. This supports
 * the common \f$S(1.0)=0\f$ convention without producing invalid coefficients.
 */
class Saturation {
  public:
    /** @brief enumeration of saturation types
     */
    enum class SaturationType {
        NONE,
        QUADRATIC,
        SCALED_QUADRATIC,
        CUTOFF_SCALED_QUADRATIC,
        EXPONENTIAL,
        LINEAR
    };

    /** Saturation value and its derivative at the same input point. */
    struct Evaluation {
        double value;
        double derivative;
    };

  private:
    double s10 = 0.0;  //!< s10 parameter
    double s12 = 0.0;  //!< s12 parameter
    double A = 0.0;  //!< parameter 1 of the saturation
    double B = 0.0;  //!< parameter 2 of the saturation
    std::function<double(double)> satFunc;  //!< the function that calculates the saturated value
    std::function<double(double)> derivFunc;  //!< the derivative of the saturation

    SaturationType type;  //!< the type of the saturation
  public:
    /** construction saturation from saturation type
     * @details constructor is converting type
     * @param[in] sT saturation Type
     */
    explicit Saturation(SaturationType sT = SaturationType::SCALED_QUADRATIC);
    /** construct from string naming saturation type
     *@param[in] satType a string containing the type of the saturation*/
    explicit Saturation(const std::string& satType);
    Saturation(const Saturation& other);
    Saturation(Saturation&& other) noexcept;
    Saturation& operator=(const Saturation& other);
    Saturation& operator=(Saturation&& other) noexcept;
    ~Saturation() = default;
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
    void setType(SaturationType sT);
    /** update the saturation function by a string*/
    void setType(const std::string& stype);
    /** get the saturation function type by enumeration*/
    SaturationType getType() const;
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
     * @return the derivative of the saturation level with respect to the input
     */
    double deriv(double val) const;
    /** @brief Compute the saturation value and derivative together.
     * @param[in] val input value
     * @return saturation value and derivative evaluated on the same characteristic
     */
    Evaluation evaluate(double val) const;
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
