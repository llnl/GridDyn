/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "saturation.h"

#include <cmath>
#include <string>

namespace utilities {
saturation::saturation(satType_t sT): type(sT)
{
    loadFunctions();
    computeParam();
}

saturation::saturation(const std::string& satType): type(satType_t::none)
{
    setType(satType);
    loadFunctions();
    computeParam();
}

void saturation::setType(const std::string& stype)
{
    if (stype == "none") {
        type = satType_t::none;
    } else if (stype == "quadratic") {
        type = satType_t::quadratic;
    } else if (stype == "scaled_quadratic") {
        type = satType_t::scaled_quadratic;
    } else if (stype == "exponential") {
        type = satType_t::exponential;
    } else if (stype == "linear") {
        type = satType_t::linear;
    }
    loadFunctions();
    computeParam();
}

void saturation::setParam(double S1, double S2)
{
    s10 = S1;
    s12 = S2;
    computeParam();
}

void saturation::setParam(double V1, double S1, double V2, double S2)
{
    switch (type) {
        case satType_t::quadratic: {
            double ssv = sqrt(S1 / S2);
            A = -(V2 * ssv - V1) / (V1 - ssv);
            B = S1 / ((V1 - A) * (V1 - A));
        } break;
        case satType_t::scaled_quadratic: {
            double ssv = sqrt((S1 * V1) / (S2 * V2));
            A = -(V2 * ssv - V1) / (V1 - ssv);
            B = S1 / ((V1 - A) * (V1 - A));
        } break;
        case satType_t::exponential:
            A = log(S1 / S2) / log(V1 / V2);
            B = S1 / (pow(V1, A));
            break;
        case satType_t::linear:
            B = (S2 - S1) / (V2 - V1);
            A = V1 - S1 / B;
            break;
        case satType_t::none:
        default:
            A = 0;
            B = 0;
            break;
    }
    s10 = compute(1.0);
    s12 = compute(1.2);
}

void saturation::setType(satType_t sT)
{
    type = sT;
    loadFunctions();
    computeParam();
}

saturation::satType_t saturation::getType() const
{
    return type;
}
double saturation::operator()(double val) const
{
    return satFunc(val);
}
double saturation::compute(double val) const
{
    return satFunc(val);
}
double saturation::deriv(double val) const
{
    return derivFunc(val);
}
double saturation::inv(double val) const
{
    if (val < 0.00001) {
        return 0.5;
    }
    double ret = 0.5;
    switch (type) {
        case satType_t::quadratic:
            ret = sqrt(val / B) + A;
            break;
        case satType_t::scaled_quadratic: {
            double temp = (2 * A + val / B);
            ret = (temp + sqrt(temp - 4 * A * A)) / 2;
            break;
        }
        case satType_t::exponential:
            ret = pow(val / B, 1 / A);
            break;
        case satType_t::linear:
            ret = A + val / B;
            break;
        case satType_t::none:
        default:
            break;
    }
    return ret;
}

void saturation::computeParam()
{
    switch (type) {
        case satType_t::quadratic: {
            double ssv = sqrt(s10 / s12);
            A = -(1.2 * ssv - 1.0) / (1.0 - ssv);
            B = s10 / ((1.0 - A) * (1.0 - A));
            break;
        }
        case satType_t::scaled_quadratic: {
            double ssv = sqrt((s10 * 1.0) / (s12 * 1.2));
            A = -(1.2 * ssv - 1.0) / (1.0 - ssv);
            B = s10 / ((1.0 - A) * (1.0 - A));
            break;
        }
        case satType_t::exponential:
            A = -log(s10 / s12) / log(1.2);
            B = s10;
            break;
        case satType_t::linear:
            B = (s12 - s10) / 0.2;
            A = 1.0 - s10 / B;
            break;
        case satType_t::none:
        default:
            A = 0;
            B = 0;
            break;
    }
}

void saturation::loadFunctions()
{
    switch (type) {
        case satType_t::quadratic:
            satFunc = [this](double val) { return (B * (val - A) * (val - A)); };
            derivFunc = [this](double val) { return (2 * B * (val - A)); };
            break;
        case satType_t::scaled_quadratic:
            satFunc = [this](double val) { return (B * (val - A) * (val - A) / val); };
            derivFunc = [this](double val) {
                double v1 = (val - A);
                return (B * (2 * val * v1 - v1 * v1) / (val * val));
            };
            break;
        case satType_t::exponential:
            satFunc = [this](double val) { return (B * pow(val, A)); };
            derivFunc = [this](double val) { return (A * B * pow(val, A - 1)); };
            break;
        case satType_t::linear:
            satFunc = [this](double val) { return (val <= A) ? 0 : (B * (val - A)); };
            derivFunc = [this](double val) { return (val <= A) ? 0 : B; };
            break;
        case satType_t::none:
        default:
            satFunc = [](double) { return 0; };
            derivFunc = [](double) { return 0; };
            break;
    }
}

}  // namespace utilities
