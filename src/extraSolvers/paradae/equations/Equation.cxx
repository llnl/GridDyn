/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Equation.h"

#include "../math/SVector.h"
#include <algorithm>
#include <cmath>

namespace griddyn {
namespace paradae {

    using namespace std;

    Equation::Equation(): nb_calls(0), nb_calls_root(0), nb_calls_jac(0), name("undefined") {}

    bool Equation::CheckAllRoots(IPoly& P,
                                 Real tlo,
                                 Vector& glo,
                                 Real& thi,
                                 Vector& ghi,
                                 Vector& state)
    {
        SVector iroot(roots.GetNRoots());
        bool scheduled_roots = false, unscheduled_roots = false;
        Real t_scheduled = thi;
        Real t_unscheduled = thi;
        int idx_scheduled = -1;

        if (roots.HasSRoots())
            scheduled_roots = CheckScheduledRoots(tlo, t_scheduled, idx_scheduled);

        if (roots.HasURoots())
            unscheduled_roots =
                CheckUnscheduledRoots(P, tlo, glo, t_unscheduled, ghi, iroot, state);

        if (scheduled_roots && unscheduled_roots) {
            if (abs(t_scheduled - t_unscheduled) < roots.tol) {
                thi = (std::min)(t_scheduled, t_unscheduled);
                iroot(idx_scheduled) = 1;
            } else if (t_unscheduled < t_scheduled)
                scheduled_roots = false;
            else
                unscheduled_roots = false;
        }
        if (scheduled_roots && !unscheduled_roots) {
            thi = t_scheduled;
            iroot.Fill(0);
            iroot(idx_scheduled) = 1;
        }
        if (unscheduled_roots && !scheduled_roots) {
            thi = t_unscheduled;
        }
        if (scheduled_roots || unscheduled_roots) {
            this->root_crossings(iroot, state);
            return true;
        }
        return false;
    }

    // Return true if we crossed a scheduled root. Return the time of the root in
    // 'thi' and return the index of the root in 'i'
    bool Equation::CheckScheduledRoots(Real tlo, Real& thi, int& i)
    {
        for (i = 0; i < roots.n_sroots; i++)
            if (roots.is_active(i))
                if (roots.t_sroot(i) - tlo > 1e-12 && thi - roots.t_sroot(i) >= 0) {
                    thi = roots.t_sroot(i);
                    return true;
                }
        i = -1;
        return false;
    }

    // Return true if root found. Return the time of the root, and modifie the state. At the end,
    // glo=ghi
    bool Equation::CheckUnscheduledRoots(IPoly& P,
                                         Real tlo,
                                         Vector& glo,
                                         Real& thi,
                                         Vector& ghi,
                                         Vector& iroot,
                                         Vector& state)
    {
        Real maxfrac = 0, gfrac;
        bool zroot = false;
        bool sgnchg = false;
        int idx = -1;
        int nroots = roots.n_uroots;
        Real tol = roots.tol;

        for (int i = 0; i < nroots; i++) {
            if (roots.is_active(i + roots.n_sroots)) {
                if (abs(ghi(i)) < tol) {
                    if (roots.dir_root(i) * glo(i) <= 0) {
                        zroot = true;
                        iroot(i + roots.n_sroots) = (glo(i) > 0) ? -1 : 1;  // TODO check
                    }
                } else {
                    if (glo(i) * ghi(i) < 0 && glo(i) * roots.dir_root(i) <= 0) {
                        gfrac = abs(ghi(i) / (ghi(i) - glo(i)));
                        if (gfrac > maxfrac) {
                            sgnchg = true;
                            maxfrac = gfrac;
                            idx = i;
                        }
                    }
                }
            }
        }

        if (!sgnchg) return zroot;

        Real alpha = 1, tmid, fracint, fracsub;
        int side = 0, side_prev = -1;
        SVector yy(P.GetXSize()), yp(P.GetXSize()), gmid(nroots);
        while (abs(thi - tlo) >= tol) {
            if (side_prev == side)
                alpha = (side == 2) ? 2.0 * alpha : 0.5 * alpha;
            else
                alpha = 1;
            tmid = thi - (thi - tlo) * ghi(idx) / (ghi(idx) - alpha * glo(idx));

            if (abs(tmid - tlo) < 0.5 * tol) {
                fracint = abs(thi - tlo) / tol;
                fracsub = (fracint > 5) ? 0.1 : 0.5 / fracint;
                tmid = tlo + fracsub * (thi - tlo);
            }
            if (abs(thi - tmid) < 0.5 * tol) {
                fracint = abs(thi - tlo) / tol;
                fracsub = (fracint > 5) ? 0.1 : 0.5 / fracint;
                tmid = thi - fracsub * (thi - tlo);
            }
            P.GetValueY(tmid, yy);
            P.GetValueDY(tmid, yp);
            this->root_functions(tmid, yy, yp, state, gmid);

            maxfrac = 0;
            zroot = false;
            sgnchg = false;
            side_prev = side;
            for (int i = 0; i < nroots; i++) {
                if (roots.is_active(i + roots.n_sroots)) {
                    if (abs(gmid(i)) < tol) {
                        if (roots.dir_root(i) * glo(i) <= 0) zroot = true;
                    } else {
                        if (glo(i) * gmid(i) < 0 && glo(i) * roots.dir_root(i) <= 0) {
                            gfrac = abs(gmid(i) / (gmid(i) - glo(i)));
                            if (gfrac > maxfrac) {
                                sgnchg = true;
                                maxfrac = gfrac;
                                idx = i;
                            }
                        }
                    }
                }
            }
            if (sgnchg) {
                thi = tmid;
                ghi.CopyData(gmid);
                side = 1;
                if (abs(thi - tlo) < tol) break;
                continue;
            }
            if (zroot) {
                thi = tmid;
                ghi.CopyData(gmid);
                break;
            }
            tlo = tmid;
            glo.CopyData(gmid);
            side = 2;
            if (abs(thi - tlo) < tol) break;
        }
        for (int i = 0; i < nroots; i++) {
            iroot(i + roots.n_sroots) = 0;
            if ((roots.dir_root(i) * glo(i) <= 0) && (abs(ghi(i)) < tol || glo(i) * ghi(i) < 0))
                iroot(i + roots.n_sroots) = (glo(i) > 0) ? -1 : 1;
        }
        return true;
    }

    void Equation::jacobian_ypcdy(const Real t,
                                  const Vector& y,
                                  const Vector& dy,
                                  const Vector& state,
                                  const Real cj,
                                  Matrix& J)
    {
        if (cj == 0)
            jacobian_y(t, y, dy, state, J, false);
        else {
            jacobian_dy(t, y, dy, state, J, false);
            J *= cj;
            jacobian_y(t, y, dy, state, J, true);
        }
    }

    void Equation::jacobian_y(const Real t,
                              const Vector& y,
                              const Vector& dy,
                              const Vector& state,
                              Matrix& Jy,
                              bool add)
    {
        approx_jacobian_y(t, y, dy, state, Jy, add);
    }

    void Equation::approx_jacobian_y(const Real t,
                                     const Vector& y,
                                     const Vector& dy,
                                     const Vector& state,
                                     Matrix& Jy,
                                     bool add)
    {
        SVector Fydy(y.GetM(), 0.0);
        function(t, y, dy, state, Fydy);
        approx_jacobian_y(t, y, dy, Fydy, state, Jy, add);
    }

    void Equation::approx_jacobian_y(const Real t,
                                     const Vector& y,
                                     const Vector& dy,
                                     const Vector& Fydy,
                                     const Vector& state,
                                     Matrix& Jy,
                                     bool add)
    {
        Real h = y.Norm2() / (100000. * n);
        SVector yh(y);
        SVector fyh(n, 0.0);

        if (h < 1e-10) h = 1e-10;
        for (int j = 0; j < n; j++) {
            yh(j) += h;
            this->function(t, yh, dy, state, fyh);
            fyh.AXPBY(-1.0 / h, 1.0 / h, Fydy);

            for (int i = 0; i < n; i++) {
                if (add)
                    Jy(i, j) += fyh(i);
                else
                    Jy(i, j) = fyh(i);
            }
            yh(j) -= h;
        }
    }

    void Equation::jacobian_dy(const Real t,
                               const Vector& y,
                               const Vector& dy,
                               const Vector& state,
                               Matrix& Jy,
                               bool add)
    {
        approx_jacobian_dy(t, y, dy, state, Jy, add);
    }

    void Equation::approx_jacobian_dy(const Real t,
                                      const Vector& y,
                                      const Vector& dy,
                                      const Vector& state,
                                      Matrix& Jy,
                                      bool add)
    {
        SVector Fydy(y.GetM(), 0.0);
        function(t, y, dy, state, Fydy);
        approx_jacobian_dy(t, y, dy, Fydy, state, Jy, add);
    }

    void Equation::approx_jacobian_dy(const Real t,
                                      const Vector& y,
                                      const Vector& dy,
                                      const Vector& Fydy,
                                      const Vector& state,
                                      Matrix& Jy,
                                      bool add)
    {
        Real h = dy.Norm2() / (1000. * n);
        SVector dyh(dy);
        SVector fdyh(n, 0.0);

        if (h < 1e-7) h = 1e-7;
        for (int j = 0; j < n; j++) {
            dyh(j) += h;
            this->function(t, y, dyh, state, fdyh);
            fdyh.AXPBY(-1.0 / h, 1.0 / h, Fydy);

            for (int i = 0; i < n; i++) {
                if (add)
                    Jy(i, j) += fdyh(i);
                else
                    Jy(i, j) = fdyh(i);
            }
            dyh(j) -= h;
        }
    }
}  // namespace paradae
}  // namespace griddyn
