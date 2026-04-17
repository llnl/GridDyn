/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace utilities {
/** class to define a lookup table based operating boundary, for min and max limits, values are
 * interpolated in intermediate points with a linear interpolator*/
class OperatingBoundary {
  private:
    /** simple structor for containing points*/
    using opPoint = struct {
        double ind;  //!< independent data point
        double depMin;  //!< dependent variable max
        double depMax;  //!< dependent variable min
    };
    double indMin = -1e47;  //!< the independent variable lower limit
    double indMax = 1e47;  //!< the independent variable upper limit

    // these variables are all for keeping a cache

    mutable double cRangeLow = 1e47;  //!< the current range lower limit
    mutable double cRangeHigh = -1e47;  //!< the current range upper limit

    mutable double depMin = -1e47;  //!< the lower limit of the dependent variable
    mutable double depMax = 1e47;  //!< the upper limit of the dependent variable
    mutable double slopeMin = 0.0;  //!< the rate of change of the lower bound
    mutable double slopeMax = 0.0;  //!< the rate of change in the upper bound
    std::vector<opPoint> operatingPoints;  //!< storage for the operating points
    mutable int lastIndex = 0;  //!< the last index

  public:
    /** default constructor*/
    OperatingBoundary() = default;
    /** constructor defining a bounding box*/
    OperatingBoundary(double indMn, double indMx, double depMn, double depMx);
    /** add an intermediate point
    @param[in] indPt the independent variable value
    @param[in] depLow the lower bound
    @param[in] depHigh the upper bound
    */
    void addPoint(double indPt, double depLow, double depHigh);

    /** add a set of points to the upper and lower bounds
    @param[in] indPts the independent variable values
    @param[in] lowPts  vector of values for the lower bound
    @param[in] highPts vector of values for the upper bound
    */
    void addPoints(const std::vector<double>& indPts,
                   const std::vector<double>& lowPts,
                   const std::vector<double>& highPts);
    /** set range of valid independent values*/
    void setValidRange(double iMin, double iMax);

    /** get the upper bound for a given value*/
    double getMax(double val) const;
    /** get the lower bound for a given value*/
    double getMin(double val) const;
    /** get both the lower and upper bounds*/
    std::pair<double, double> getLimits(double val) const;
    /** get the rate of change in upper bound for a given value (aka the slope of the
     * interpolator)*/
    double dMaxROC(double val) const;
    /** get the rate of change in lower bound for a given value (aka the slope of the
     * interpolator)*/
    double dMinROC(double val) const;
    /** get the rate of change in lower and upper bounds for a given value (aka the slope of the
     * interpolator)*/
    std::pair<double, double> getLimitsROC(double val) const;
    /** clear the table*/
    void clear();

  private:
    /** function to update the cache values*/
    void updateRange(double val) const;
};

}  // namespace utilities
