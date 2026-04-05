/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

/** @file
 *  @brief define some operations related to matrixData objects
 */
#pragma once

#include <complex>
#include <type_traits>
#include <vector>

// simple template class for selecting the an appropriate value based on Phase
template<class X>
constexpr X phaseSelector(char phase, X valA, X valB, X valC, X def) noexcept
{
    return ((phase == 'a') || (phase == '1')) ? valA :
        ((phase == 'b') || (phase == '2'))    ? valB :
        ((phase == 'c') || (phase == '3'))    ? valC :
                                                def;
}

constexpr auto k_PI = 3.14159265358979323846;
/** multiplier constants for representation change*/
static constexpr std::complex<double> alpha0 = std::complex<double>(1.0, 0);
static const std::complex<double> alpha = std::polar(1.0, -2.0 * k_PI / 3.0);
static const std::complex<double> alpha2 = alpha * alpha;

// constants for rotating a complex number by +120 and -120 degrees
static const std::complex<double> rotp120(-0.5, sqrt(3.0) / 2.0);
static const std::complex<double> rotn120(-0.5, -sqrt(3.0) / 2.0);

// TODO::PT add enable if statements
template<class X>
X generate3PhaseVector(const X& val)
{
    X vvals;
    vvals.resize(6);

    auto A = std::polar(val[0], val[1]);
    auto B = A * rotn120;
    auto C = A * rotp120;
    vvals[0] = A.real();
    vvals[1] = A.imag();
    vvals[2] = B.real();
    vvals[3] = B.imag();
    vvals[4] = C.real();
    vvals[5] = C.imag();
    return vvals;
}

// TODO::PT add enable if statements
template<class X>
X generate3PhasePolarVector(const X& val)
{
    X vvals;
    vvals.resize(6);

    auto A = std::polar(val[0], val[1]);
    auto B = A * rotn120;
    auto C = A * rotp120;
    vvals[0] = std::abs(A);
    vvals[1] = std::arg(A);
    vvals[2] = std::abs(B);
    vvals[3] = std::arg(B);
    vvals[4] = std::abs(C);
    vvals[5] = std::arg(C);
    return vvals;
}

template<class X>
X ABCtoPNZ_R(const X& abcR, const X& abcI)
{
    std::complex<double> A(abcR[0], abcI[0]);
    std::complex<double> B(abcR[1], abcI[1]);
    std::complex<double> C(abcR[2], abcI[2]);

    double P = abcR[0] + abcR[1] + abcR[2];

    std::complex<double> N = A + B * alpha + C * alpha2;
    std::complex<double> Z = A + B * alpha2 + C * alpha;

    return {P, N.real(), Z.real()};
}

template<class X>
X ABCtoPNZ_I(const X& abcR, const X& abcI)
{
    std::complex<double> A(abcR[0], abcI[0]);
    std::complex<double> B(abcR[1], abcI[1]);
    std::complex<double> C(abcR[2], abcI[2]);

    double Q = abcI[0] + abcI[1] + abcI[2];

    std::complex<double> N = A + B * alpha + C * alpha2;
    std::complex<double> Z = A + B * alpha2 + C * alpha;

    return {Q, N.imag(), Z.imag()};
}

template<class X>
X ThreePhasePowerPolar(const X& V, const X& I)
{
    auto V1 = std::polar(V[0], V[1] * k_PI / 180.0);
    auto V2 = std::polar(V[2], V[3] * k_PI / 180.0);
    auto V3 = std::polar(V[4], V[5] * k_PI / 180.0);
    auto I1 = std::polar(I[0], I[1] * k_PI / 180.0);
    auto I2 = std::polar(I[2], I[3] * k_PI / 180.0);
    auto I3 = std::polar(I[4], I[5] * k_PI / 180.0);

    auto P1 = V1 * std::conj(I1);
    auto P2 = V2 * std::conj(I2);
    auto P3 = V3 * std::conj(I3);

    return {P1.real(), P1.imag(), P2.real(), P2.imag(), P3.real(), P3.imag()};
}
