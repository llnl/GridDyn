/* Copyright (c) 2014-2026, Lawrence Livermore National Security
 * SPDX-License-Identifier: BSD-3-Clause */
#include "GenModelGENTPJ.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <complex>
namespace griddyn::genmodels {
namespace {
    struct Terms {
        double ds, qs, xdpp, xqpp, eq1, ed1, eq2, ed2, psid, psiq;
    };
    constexpr std::size_t jacobianVariableCount = 12;
    struct Dual {
        double value{};
        std::array<double, jacobianVariableCount> derivative{};
        Dual() = default;
        Dual(double v): value(v) {}
        Dual(double v, std::size_t i): value(v) { derivative[i] = 1.0; }
    };
    Dual operator+(Dual a, Dual b)
    {
        Dual r(a.value + b.value);
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] = a.derivative[i] + b.derivative[i];
        return r;
    }
    Dual operator-(Dual a, Dual b)
    {
        Dual r(a.value - b.value);
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] = a.derivative[i] - b.derivative[i];
        return r;
    }
    Dual operator-(Dual a)
    {
        for (auto& v : a.derivative)
            v = -v;
        a.value = -a.value;
        return a;
    }
    Dual operator*(Dual a, Dual b)
    {
        Dual r(a.value * b.value);
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] = a.derivative[i] * b.value + a.value * b.derivative[i];
        return r;
    }
    Dual operator/(Dual a, Dual b)
    {
        Dual r(a.value / b.value);
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] =
                (a.derivative[i] * b.value - a.value * b.derivative[i]) / (b.value * b.value);
        return r;
    }
    Dual squareRoot(Dual a)
    {
        const double root = std::sqrt(a.value);
        Dual r(root);
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] = a.derivative[i] / (2.0 * root);
        return r;
    }
    Dual sine(Dual a)
    {
        Dual r(std::sin(a.value));
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] = std::cos(a.value) * a.derivative[i];
        return r;
    }
    Dual cosine(Dual a)
    {
        Dual r(std::cos(a.value));
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] = -std::sin(a.value) * a.derivative[i];
        return r;
    }
    Dual saturation(Dual a, const utilities::Saturation& sat)
    {
        const auto e = sat.evaluate(a.value);
        Dual r(e.value);
        for (std::size_t i = 0; i < jacobianVariableCount; ++i)
            r.derivative[i] = e.derivative * a.derivative[i];
        return r;
    }
    Terms terms(double vd,
                double vq,
                double id,
                double iq,
                const double* s,
                double rs,
                double xl,
                double xd,
                double xq,
                double xdp,
                double xqp,
                double xdpp,
                double xqpp,
                double kis,
                const utilities::Saturation& sat)
    {
        const double agd = vq + rs * iq - xl * id, agq = -vd - rs * id - xl * iq;
        const double se = sat.compute(std::hypot(agd, agq) + kis * std::hypot(id, iq));
        Terms t;
        t.ds = 1.0 + se;
        t.qs = 1.0 + (xq / xd) * se;
        t.xdpp = (xdpp - xl) / t.ds + xl;
        t.xqpp = (xqpp - xl) / t.qs + xl;
        t.psid = s[4];
        t.psiq = s[5];
        t.eq1 = (-t.psid * (xd - xdp) + s[3] * (xd - xdpp)) / (xdp - xdpp);
        t.ed1 = (t.psiq * (xq - xqp) + s[2] * (xq - xqpp)) / (xqp - xqpp);
        t.eq2 = (t.psid - s[3] - id * (xdp - xdpp) / t.ds) * (xd - xdpp) / (xdp - xdpp);
        t.ed2 = -(s[2] + t.psiq) * (xq - xqpp) / (xqp - xqpp) - iq * (xq - xqpp) / t.qs;
        return t;
    }
}  // namespace
GenModelGENTPJ::GenModelGENTPJ(const std::string& n): GenModel5(n)
{
    S10 = 0;
    S12 = 1;
    sat.setType(utilities::Saturation::SaturationType::CUTOFF_SCALED_QUADRATIC);
    sat.setParam(S10, S12);
}
CoreObject* GenModelGENTPJ::clone(CoreObject* o) const
{
    auto* c = cloneBase<GenModelGENTPJ, GenModel5>(this, o);
    if (c) {
        c->Kis = Kis;
        c->sat = sat;
    }
    return c ? c : o;
}
void GenModelGENTPJ::dynObjectInitializeA(CoreTime, std::uint32_t)
{
    if (H <= 0 || Tdop <= 0 || Tqop <= 0 || Tdopp <= 0 || Tqopp <= 0 || Xd <= Xdp || Xq <= Xqp ||
        Xdp <= Xdpp || Xqp <= Xqpp)
        throw InvalidParameterValue("GENTPJ machine parameters");
    offsets.local().local.diffSize = 6;
    offsets.local().local.algSize = 2;
    offsets.local().local.jacSize = 96;
}
void GenModelGENTPJ::dynObjectInitializeB(const IOdata& in, const IOdata& out, IOdata& fs)
{
    const std::complex<double> voltage = std::polar(in[0], in[1]);
    const std::complex<double> current = std::complex<double>(out[0], -out[1]) / std::conj(voltage);
    auto values = [&](double delta,
                      double& directCurrent,
                      double& quadratureCurrent,
                      double& directVoltage,
                      double& quadratureVoltage,
                      Terms& q) {
        const auto dq = std::conj(current * std::polar(1.0, -delta));
        quadratureCurrent = std::real(dq);
        directCurrent = -std::imag(dq);
        directVoltage = -in[0] * std::sin(delta - in[1]);
        quadratureVoltage = in[0] * std::cos(delta - in[1]);
        double s[6]{};
        q = terms(directVoltage,
                  quadratureVoltage,
                  directCurrent,
                  quadratureCurrent,
                  s,
                  Rs,
                  Xl,
                  Xd,
                  Xq,
                  Xdp,
                  Xqp,
                  Xdpp,
                  Xqpp,
                  Kis,
                  sat);
    };
    double delta = std::arg(voltage + current * std::complex<double>(Rs, Xdpp));
    for (int k = 0; k < 12; ++k) {
        double directCurrent, quadratureCurrent, directVoltage, quadratureVoltage;
        double perturbedDirectCurrent, perturbedQuadratureCurrent;
        double perturbedDirectVoltage, perturbedQuadratureVoltage;
        Terms q, qp;
        values(delta, directCurrent, quadratureCurrent, directVoltage, quadratureVoltage, q);
        const double f = directVoltage + Rs * directCurrent + q.xqpp * quadratureCurrent +
            quadratureCurrent * (Xq - Xqpp) / q.qs;
        values(delta + 1e-6,
               perturbedDirectCurrent,
               perturbedQuadratureCurrent,
               perturbedDirectVoltage,
               perturbedQuadratureVoltage,
               qp);
        const double fp = perturbedDirectVoltage + Rs * perturbedDirectCurrent +
            qp.xqpp * perturbedQuadratureCurrent + perturbedQuadratureCurrent * (Xq - Xqpp) / qp.qs;
        delta -= (f / (fp - f)) * 1e-6;
    }
    double directCurrent, quadratureCurrent, directVoltage, quadratureVoltage;
    Terms q;
    values(delta, directCurrent, quadratureCurrent, directVoltage, quadratureVoltage, q);
    const double psid = quadratureVoltage + Rs * quadratureCurrent - q.xdpp * directCurrent;
    const double psiq = directVoltage + Rs * directCurrent + q.xqpp * quadratureCurrent;
    auto* z = m_state.data();
    z[0] = directCurrent;
    z[1] = quadratureCurrent;
    z[2] = delta;
    z[3] = 1.0;
    z[4] = -psiq * (Xq - Xqp) / (Xq - Xqpp);
    z[5] = psid - directCurrent * (Xdp - Xdpp) / q.ds;
    z[6] = psid;
    z[7] = psiq;
    q = terms(directVoltage,
              quadratureVoltage,
              directCurrent,
              quadratureCurrent,
              z + 2,
              Rs,
              Xl,
              Xd,
              Xq,
              Xdp,
              Xqp,
              Xdpp,
              Xqpp,
              Kis,
              sat);
    fs[genModelEftInLocation] = q.ds * q.eq1;
    fs[genModelPmechInLocation] = (directVoltage + Rs * directCurrent) * directCurrent +
        (quadratureVoltage + Rs * quadratureCurrent) * quadratureCurrent;
    Vd = directVoltage;
    Vq = quadratureVoltage;
}
void GenModelGENTPJ::set(std::string_view p, double v, units::unit u)
{
    if (p == "kis")
        Kis = v;
    else if (p == "s10" || p == "s1") {
        S10 = v;
        sat.setParam(S10, S12);
    } else if (p == "s12") {
        S12 = v;
        sat.setParam(S10, S12);
    } else
        GenModel5::set(p, v, u);
}
double GenModelGENTPJ::get(std::string_view p, units::unit u) const
{
    if (p == "kis") return Kis;
    return GenModel5::get(p, u);
}
stringVec GenModelGENTPJ::localStateNames() const
{
    return {"id", "iq", "delta", "freq", "epd", "epq", "psippd", "psippq"};
}
void GenModelGENTPJ::algebraicUpdate(const IOdata& in,
                                     const StateData& sd,
                                     double up[],
                                     const SolverMode& sm,
                                     double)
{
    auto l = offsets.getLocations(sd, up, sm, this);
    updateLocalCache(in, sd, sm);
    auto t = terms(Vd,
                   Vq,
                   l.algStateLoc[0],
                   l.algStateLoc[1],
                   l.diffStateLoc,
                   Rs,
                   Xl,
                   Xd,
                   Xq,
                   Xdp,
                   Xqp,
                   Xdpp,
                   Xqpp,
                   Kis,
                   sat);
    gmlc::utilities::solve2x2(
        Rs, t.xqpp, -t.xdpp, Rs, t.psiq - Vd, t.psid - Vq, l.destLoc[0], l.destLoc[1]);
}
void GenModelGENTPJ::derivative(const IOdata& in,
                                const StateData& sd,
                                double d[],
                                const SolverMode& sm)
{
    if (isAlgebraicOnly(sm)) return;
    auto l = offsets.getLocations(sd, d, sm, this);
    auto* t = l.algStateLoc;
    auto* s = l.diffStateLoc;
    auto* v = l.destDiffLoc;
    auto q = terms(Vd, Vq, t[0], t[1], s, Rs, Xl, Xd, Xq, Xdp, Xqp, Xdpp, Xqpp, Kis, sat);
    const double te = (q.psid + q.xdpp * t[0]) * t[1] + (q.psiq - q.xqpp * t[1]) * t[0];
    v[0] = systemBaseFrequency * (s[1] - 1);
    v[1] = (in[genModelPmechInLocation] - te - D * (s[1] - 1)) / (2 * H);
    v[2] = -q.qs * q.ed1 / Tqop;
    v[3] = (in[genModelEftInLocation] - q.ds * q.eq1) / Tdop;
    v[4] = -q.ds * (Xdp - Xdpp) / (Xd - Xdpp) * q.eq2 / Tdopp;
    v[5] = q.qs * (Xqp - Xqpp) / (Xq - Xqpp) * q.ed2 / Tqopp;
}
void GenModelGENTPJ::residual(const IOdata& in,
                              const StateData& sd,
                              double r[],
                              const SolverMode& sm)
{
    auto l = offsets.getLocations(sd, r, sm, this);
    updateLocalCache(in, sd, sm);
    if (hasAlgebraic(sm)) {
        auto q = terms(Vd,
                       Vq,
                       l.algStateLoc[0],
                       l.algStateLoc[1],
                       l.diffStateLoc,
                       Rs,
                       Xl,
                       Xd,
                       Xq,
                       Xdp,
                       Xqp,
                       Xdpp,
                       Xqpp,
                       Kis,
                       sat);
        l.destLoc[0] = Vd + Rs * l.algStateLoc[0] + q.xqpp * l.algStateLoc[1] - q.psiq;
        l.destLoc[1] = Vq + Rs * l.algStateLoc[1] - q.xdpp * l.algStateLoc[0] - q.psid;
    }
    if (hasDifferential(sm)) {
        derivative(in, sd, r, sm);
        for (index_t k = 0; k < 6; ++k)
            l.destDiffLoc[k] -= l.dstateLoc[k];
    }
}
void GenModelGENTPJ::jacobianElements(const IOdata& in,
                                      const StateData& sd,
                                      MatrixData<double>& md,
                                      const IOlocs& il,
                                      const SolverMode& sm)
{
    auto l = offsets.getLocations(sd, sm, this);
    if (!hasAlgebraic(sm) && !hasDifferential(sm)) return;
    std::array<Dual, jacobianVariableCount> x{};
    x[0] = Dual(l.algStateLoc[0], 0);
    x[1] = Dual(l.algStateLoc[1], 1);
    for (std::size_t k = 0; k < 6; ++k)
        x[k + 2] = Dual(l.diffStateLoc[k], k + 2);
    x[8] = Dual(in[0], 8);
    x[9] = Dual(in[1], 9);
    x[10] = Dual(in[2], 10);
    x[11] = Dual(in[3], 11);
    const Dual vd = -x[8] * sine(x[2] - x[9]), vq = x[8] * cosine(x[2] - x[9]);
    const Dual agd = vq + Rs * x[1] - Xl * x[0], agq = -vd - Rs * x[0] - Xl * x[1],
               se = saturation(squareRoot(agd * agd + agq * agq) +
                                   Kis * squareRoot(x[0] * x[0] + x[1] * x[1]),
                               sat),
               ds = Dual(1) + se, qs = Dual(1) + (Xq / Xd) * se, xdpp = (Xdpp - Xl) / ds + Xl,
               xqpp = (Xqpp - Xl) / qs + Xl;
    const Dual eq1 = (-x[6] * (Xd - Xdp) + x[5] * (Xd - Xdpp)) / (Xdp - Xdpp),
               ed1 = (x[7] * (Xq - Xqp) + x[4] * (Xq - Xqpp)) / (Xqp - Xqpp),
               eq2 = (x[6] - x[5] - x[0] * (Xdp - Xdpp) / ds) * (Xd - Xdpp) / (Xdp - Xdpp),
               ed2 = -(x[4] + x[7]) * (Xq - Xqpp) / (Xqp - Xqpp) - x[1] * (Xq - Xqpp) / qs;
    const Dual te = (x[6] + xdpp * x[0]) * x[1] + (x[7] - xqpp * x[1]) * x[0];
    std::array<Dual, 8> f{vd + Rs * x[0] + xqpp * x[1] - x[7],
                          vq + Rs * x[1] - xdpp * x[0] - x[6],
                          systemBaseFrequency * (x[3] - 1),
                          (x[11] - te - D * (x[3] - 1)) / (2 * H),
                          -qs * ed1 / Tqop,
                          (x[10] - ds * eq1) / Tdop,
                          -ds * (Xdp - Xdpp) / (Xd - Xdpp) * eq2 / Tdopp,
                          qs * (Xqp - Xqpp) / (Xq - Xqpp) * ed2 / Tqopp};
    const std::array<index_t, 8> rows{l.algOffset,
                                      l.algOffset + 1,
                                      l.diffOffset,
                                      l.diffOffset + 1,
                                      l.diffOffset + 2,
                                      l.diffOffset + 3,
                                      l.diffOffset + 4,
                                      l.diffOffset + 5};
    const std::array<index_t, 12> cols{l.algOffset,
                                       l.algOffset + 1,
                                       l.diffOffset,
                                       l.diffOffset + 1,
                                       l.diffOffset + 2,
                                       l.diffOffset + 3,
                                       l.diffOffset + 4,
                                       l.diffOffset + 5,
                                       il[0],
                                       il[1],
                                       il[2],
                                       il[3]};
    for (std::size_t r = 0; r < 8; ++r) {
        if ((r < 2 && !hasAlgebraic(sm)) || (r >= 2 && !hasDifferential(sm))) continue;
        for (std::size_t c = 0; c < 12; ++c)
            if (cols[c] != kNullLocation && f[r].derivative[c] != 0.0 && ((r < 2) || (c != r)))
                md.assign(rows[r], cols[c], f[r].derivative[c]);
        if (r >= 2) md.assign(rows[r], rows[r], f[r].derivative[r] - sd.cj);
    }
}
}  // namespace griddyn::genmodels
