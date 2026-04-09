/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "IVander.h"
#include <iostream>

#include "PVector.h"
#include "SVector.h"

namespace griddyn {
namespace paradae {
    using namespace std;

    IVander::IVander(int n): DenseMatrix(n)
    {
        switch (n) {
            case 2:
                Build2();
                break;
            case 3:
                Build3();
                break;
            case 4:
                Build4();
                break;
            case 5:
                Build5();
                break;
            case 6:
                Build6();
                break;
            default:
                cerr << "Not implemented" << endl;
                abort();
        }
    }

    void IVander::Build2()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = -1;
        data[3] = 1;
    }

    void IVander::Build3()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = -1.5;
        data[4] = 2;
        data[5] = -0.5;
        data[6] = 0.5;
        data[7] = -1;
        data[8] = 0.5;
    }

    void IVander::Build4()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = -11. / 6.;
        data[5] = 3;
        data[6] = -1.5;
        data[7] = 1. / 3.;
        data[8] = 1;
        data[9] = -2.5;
        data[10] = 2;
        data[11] = -0.5;
        data[12] = -1. / 6.;
        data[13] = 0.5;
        data[14] = -0.5;
        data[15] = 1. / 6.;
    }

    void IVander::Build5()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        data[5] = -25. / 12.;
        data[6] = 4;
        data[7] = -3;
        data[8] = 4. / 3.;
        data[9] = -0.25;
        data[10] = 35. / 24.;
        data[11] = -13. / 3.;
        data[12] = 4.75;
        data[13] = -7. / 3.;
        data[14] = 11. / 24.;
        data[15] = -5. / 12.;
        data[16] = 1.5;
        data[17] = -2;
        data[18] = 7. / 6.;
        data[19] = -0.25;
        data[20] = 1. / 24.;
        data[21] = -1. / 6.;
        data[22] = 0.25;
        data[23] = -1. / 6.;
        data[24] = 1. / 24.;
    }

    void IVander::Build6()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        data[5] = 0;
        data[6] = -137. / 60.;
        data[7] = 5;
        data[8] = -5;
        data[9] = 10. / 3.;
        data[10] = -1.25;
        data[11] = 0.2;
        data[12] = 1.875;
        data[13] = -77. / 12.;
        data[14] = 107. / 12.;
        data[15] = -6.5;
        data[16] = 61. / 24.;
        data[17] = -5. / 12.;
        data[18] = -17. / 24.;
        data[19] = 71. / 24.;
        data[20] = -59. / 12.;
        data[21] = 49. / 12.;
        data[22] = -41. / 24.;
        data[23] = 7. / 24.;
        data[24] = 0.125;
        data[25] = -7. / 12.;
        data[26] = 13. / 12.;
        data[27] = -1;
        data[28] = 11. / 24.;
        data[29] = -1. / 12.;
        data[30] = -1. / 120.;
        data[31] = 1. / 24.;
        data[32] = -1. / 12.;
        data[33] = 1. / 12.;
        data[34] = -1. / 24.;
        data[35] = 1. / 120.;
    }

    void IVander::Derivate(DenseMatrix& M) const
    {
        for (int i = 0; i < m - 1; i++)
            for (int j = 0; j < m; j++)
                M(i, j) = this->operator()(i + 1, j) * (i + 1);
    }

    void IVander::Interp(const SMultiVector& xn,
                         const Vector& dx,
                         SMultiVector& new_xn,
                         Vector& new_dx,
                         Real dt,
                         Real Dt) const
    {
#ifdef CHECK_MEM_OP
        if (xn.GetSSize() != m) {
            cerr << "The list of vector has incorrect size in IVander[" << m << "]::Interp(xn["
                 << xn.GetSSize() << "])" << endl;
            abort();
        }
#endif
        new_xn.Resize(m, xn.GetXSize());
        SVector coeffs(m);
        PVector subvec_xn, subvec_newxn;
        Real ratio = dt / Dt;

        new_xn.GetPVector(m - 1, subvec_newxn);
        xn.GetPVector(m - 1, subvec_xn);
        subvec_newxn.CopyData(subvec_xn);

        for (int i = 1; i < m; i++) {
            coeffs(0) = 1;
            for (int j = 1; j < m; j++)
                coeffs(j) = coeffs(j - 1) * ratio * i;
            this->MatMult(coeffs, true);

            new_xn.GetPVector(m - 1 - i, subvec_newxn);
            for (int j = 0; j < m; j++) {
                xn.GetPVector(j, subvec_xn);
                subvec_newxn.AXPBY(coeffs(m - 1 - j), 1.0, subvec_xn);
            }
        }

        coeffs(0) = 1;
        for (int j = 1; j < m; j++)
            coeffs(j) = coeffs(j - 1) * ratio * (m - 1);
        DenseMatrix deriv(m);
        this->Derivate(deriv);
        deriv.MatMult(coeffs, true);

        new_dx.Fill();
        for (int j = 0; j < m; j++) {
            xn.GetPVector(j, subvec_xn);
            new_dx.AXPBY(coeffs(m - 1 - j) / Dt, 1.0, subvec_xn);
        }
    }
}  // namespace paradae
}  // namespace griddyn
