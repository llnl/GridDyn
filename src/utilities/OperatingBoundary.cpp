/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */
#include "OperatingBoundary.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace utilities {
auto compareOp = [](const auto& p1, const auto& p2) { return (p1.ind < p2.ind); };

OperatingBoundary::OperatingBoundary(double indMn, double indMx, double depMn, double depMx):
    indMin(indMn), indMax(indMx)
{
    addPoint(indMn, depMn, depMx);
    addPoint(indMx, depMn, depMx);
    updateRange((indMn + indMx) / 2.0);
}

void OperatingBoundary::addPoint(double indPt, double depLow, double depHigh)
{
    opPoint p1{indPt, depLow, depHigh};
    auto Loc = std::lower_bound(operatingPoints.begin(), operatingPoints.end(), p1, compareOp);
    if (Loc == operatingPoints.end()) {
        operatingPoints.push_back(p1);
    } else {
        if (Loc->ind != indPt) {
            operatingPoints.insert(Loc, p1);
        } else {  // replace the point
            Loc->depMin = depLow;
            Loc->depMax = depHigh;
        }
    }
}

void OperatingBoundary::addPoints(const std::vector<double>& indPts,
                                  const std::vector<double>& lowPts,
                                  const std::vector<double>& highPts)
{
    if (indPts.size() != highPts.size()) {
        throw(
            std::invalid_argument("independent point list does not match upper bound point list"));
    }
    if (highPts.size() != lowPts.size()) {
        throw(std::invalid_argument("upper and lower bound point list sizes do not match"));
    }
    for (size_t ii = 0; ii < indPts.size(); ++ii) {
        addPoint(indPts[ii], highPts[ii], lowPts[ii]);
    }
}

void OperatingBoundary::setValidRange(double iMin, double iMax)
{
    indMax = iMax;
    indMin = iMin;
}

double OperatingBoundary::getMax(double val) const
{
    if ((val < cRangeLow) || (val > cRangeHigh)) {
        updateRange(val);
    }
    return depMax + (val - cRangeLow) * slopeMax;
}

double OperatingBoundary::getMin(double val) const
{
    if ((val < cRangeLow) || (val > cRangeHigh)) {
        updateRange(val);
    }
    return depMin + (val - cRangeLow) * slopeMin;
}

std::pair<double, double> OperatingBoundary::getLimits(double val) const
{
    if ((val < cRangeLow) || (val > cRangeHigh)) {
        updateRange(val);
    }
    return {depMin + (val - cRangeLow) * slopeMin, depMax + (val - cRangeLow) * slopeMax};
}
double OperatingBoundary::dMaxROC(double val) const
{
    if ((val < cRangeLow) || (val > cRangeHigh)) {
        updateRange(val);
    }
    return slopeMax;
}
double OperatingBoundary::dMinROC(double val) const
{
    if ((val < cRangeLow) || (val > cRangeHigh)) {
        updateRange(val);
    }
    return slopeMin;
}
std::pair<double, double> OperatingBoundary::getLimitsROC(double val) const
{
    if ((val < cRangeLow) || (val > cRangeHigh)) {
        updateRange(val);
    }
    return {slopeMin, slopeMax};
}

void OperatingBoundary::clear()
{
    operatingPoints.clear();
    indMin = -1e47;
    indMax = 1e47;

    cRangeLow = 1e47;
    cRangeHigh = -1e47;

    depMin = -1e47;
    depMax = 1e47;
    slopeMin = 0.0;
    slopeMax = 0.0;
    lastIndex = 0;
}

void OperatingBoundary::updateRange(double val) const
{
    if (val > cRangeHigh) {
        auto vLoc = std::lower_bound(operatingPoints.begin() + lastIndex,
                                     operatingPoints.end(),
                                     opPoint{val, 0, 0},
                                     compareOp);
        lastIndex = vLoc - operatingPoints.begin();
    } else {
        auto vLoc = std::lower_bound(operatingPoints.begin(),
                                     operatingPoints.begin() + lastIndex,
                                     opPoint{val, 0, 0},
                                     compareOp);
        lastIndex = vLoc - operatingPoints.begin();
    }
    if (lastIndex == 0) {
        auto& pt = operatingPoints.front();
        cRangeLow = indMin;
        cRangeHigh = pt.ind;
        depMin = pt.depMin;
        depMax = pt.depMax;
        slopeMax = 0.0;
        slopeMin = 0.0;
    } else if (lastIndex == static_cast<decltype(lastIndex)>(operatingPoints.size())) {
        auto& pt = operatingPoints.back();
        cRangeLow = pt.ind;
        cRangeHigh = indMax;
        depMin = pt.depMin;
        depMax = pt.depMax;
        slopeMax = 0.0;
        slopeMin = 0.0;
        return;
    } else {
        auto& pt1 = operatingPoints[lastIndex - 1];
        auto& pt2 = operatingPoints[lastIndex];
        cRangeLow = pt1.ind;
        cRangeHigh = pt2.ind;
        depMin = pt1.depMin;
        depMax = pt1.depMax;
        slopeMax = (pt2.depMax - depMax) / (cRangeHigh - cRangeLow);
        slopeMin = (pt2.depMin - depMin) / (cRangeHigh - cRangeLow);
    }
}

}  // namespace utilities
