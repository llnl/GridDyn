/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "IPoly.h"

#include "DenseMatrix.h"
#include "PMultiVector.h"
#include "PVector.h"
#include "SVector.h"
#include <cfloat>
#include <cmath>
#include <iostream>

namespace griddyn::paradae {
    using namespace std;

    IPoly::IPoly(const PMultiVector& tyy_, const PMultiVector& yy_): tyy(tyy_), yy(yy_)
    {
        nyy = yy.GetSSize();
        nyp = 0;
        nx = yy.GetXSize();
        min_time = DBL_MAX;
        max_time = -DBL_MAX;
        is_built = false;
    }

    IPoly::IPoly(const PMultiVector& tyy_,
                 const PMultiVector& yy_,
                 const PMultiVector& typ_,
                 const PMultiVector yp_): tyy(tyy_), yy(yy_), typ(typ_), yp(yp_)
    {
        nyy = yy.GetSSize();
        nyp = yp.GetSSize();
        nx = yy.GetXSize();
        min_time = DBL_MAX;
        max_time = -DBL_MAX;
        is_built = false;
    }

    void IPoly::Build()
    {
        SVector alltimes(nyy + nyp);
        for (int i = 0; i < nyy; i++) {
            alltimes(i) = tyy(i);
            if (tyy(i) < min_time) min_time = tyy(i);
            if (tyy(i) > max_time) max_time = tyy(i);
        }
        for (int i = 0; i < nyp; i++) {
            alltimes(i + nyy) = typ(i);
            if (tyy(i) < min_time) min_time = typ(i);
            if (tyy(i) > max_time) max_time = typ(i);
        }
        for (int i = 0; i < nyy + nyp; i++)
            alltimes(i) = (alltimes(i) - min_time) / (max_time - min_time);

        M.Clone(DenseMatrix(nyy + nyp));
        Real t;
        for (int j = 0; j < nyy; j++) {
            t = 1;
            for (int i = 0; i < nyy + nyp; i++) {
                M(i, j) = t;
                t *= alltimes(j);
            }
        }
        for (int j = 0; j < nyp; j++) {
            t = alltimes(j + nyy);
            for (int i = 1; i < nyy + nyp; i++) {
                M(i, j + nyy) = i * pow(t, i - 1) / (max_time - min_time);
            }
        }
        M.Factorize();
        is_built = true;
    }

    void IPoly::GetValueY(Real t, Vector& y)
    {
        if (!is_built) this->Build();
        if ((t < min_time) || (t > max_time))
            cout << "IPoly:Warning : you are extrapolating" << endl;
        SVector vec_t(nyy + nyp);
        Real tt = 1;
        t = (t - min_time) / (max_time - min_time);
        for (int i = 0; i < nyy + nyp; i++) {
            vec_t(i) = tt;
            tt *= t;
        }
        M.Solve(vec_t);
        y.Fill(0);
        PVector yi;
        for (int i = 0; i < nyy; i++) {
            yy.GetPVector(i, yi);
            y.AXPBY(vec_t(i), 1.0, yi);
        }
        for (int i = 0; i < nyp; i++) {
            yp.GetPVector(i, yi);
            y.AXPBY(vec_t(i + nyy), 1.0, yi);
        }
    }

    void IPoly::GetValueDY(Real t, Vector& dy)
    {
        if (!is_built) this->Build();
        if ((t < min_time) || (t > max_time))
            cout << "IPoly:Warning : you are extrapolating" << endl;
        SVector vec_t(nyy + nyp);
        t = (t - min_time) / (max_time - min_time);
        for (int i = 1; i < nyy + nyp; i++)
            vec_t(i) = i * pow(t, i - 1);
        M.Solve(vec_t);
        dy.Fill(0);
        PVector yi;
        for (int i = 0; i < nyy; i++) {
            yy.GetPVector(i, yi);
            dy.AXPBY(vec_t(i) / (max_time - min_time), 1.0, yi);
        }
        for (int i = 0; i < nyp; i++) {
            yp.GetPVector(i, yi);
            dy.AXPBY(vec_t(i + nyy) / (max_time - min_time), 1.0, yi);
        }
    }
}  // namespace griddyn::paradae
