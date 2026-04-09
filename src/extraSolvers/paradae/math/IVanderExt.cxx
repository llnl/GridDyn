/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "IVanderExt.h"
#include <iostream>

#include "PVector.h"
#include "SVector.h"

namespace griddyn {
namespace paradae {
    using namespace std;

    IVanderExt::IVanderExt(int n): DenseMatrix(n + 1)
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

    void IVanderExt::Build2()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = -2;
        data[4] = 2;
        data[5] = -1;
        data[6] = 1;
        data[7] = -1;
        data[8] = 1;
    }

    void IVanderExt::Build3()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = -2;
        data[5] = 4;
        data[6] = -2;
        data[7] = 1;
        data[8] = 1.25;
        data[9] = -4;
        data[10] = 2.75;
        data[11] = -1.5;
        data[12] = -0.25;
        data[13] = 1;
        data[14] = -0.75;
        data[15] = 0.5;
    }

    void IVanderExt::Build4()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        data[5] = -13. / 6.;
        data[6] = 4.5;
        data[7] = -4.5;
        data[8] = 13. / 6.;
        data[9] = -1;
        data[10] = 29. / 18.;
        data[11] = -5.25;
        data[12] = 7.5;
        data[13] = -139. / 36.;
        data[14] = 11. / 6.;
        data[15] = -0.5;
        data[16] = 2;
        data[17] = -3.5;
        data[18] = 2;
        data[19] = -1;
        data[20] = 1. / 18.;
        data[21] = -0.25;
        data[22] = 0.5;
        data[23] = -11. / 36.;
        data[24] = 1. / 6.;
    }

    void IVanderExt::Build5()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        data[5] = 0;
        data[6] = -7. / 3.;
        data[7] = 16. / 3.;
        data[8] = -6;
        data[9] = 16. / 3.;
        data[10] = -7. / 3.;
        data[11] = 1;
        data[12] = 95. / 48.;
        data[13] = -64. / 9.;
        data[14] = 11;
        data[15] = -32. / 3.;
        data[16] = 691. / 144.;
        data[17] = -25. / 12.;
        data[18] = -25. / 32.;
        data[19] = 31. / 9.;
        data[20] = -6.375;
        data[21] = 7;
        data[22] = -947. / 288.;
        data[23] = 35. / 24.;
        data[24] = 7. / 48.;
        data[25] = -13. / 18.;
        data[26] = 1.5;
        data[27] = -11. / 6.;
        data[28] = 131. / 144.;
        data[29] = -5. / 12.;
        data[30] = -1. / 96.;
        data[31] = 1. / 18.;
        data[32] = -1. / 8.;
        data[33] = 1. / 6.;
        data[34] = -25. / 288.;
        data[35] = 1. / 24.;
    }

    void IVanderExt::Build6()
    {
        data[0] = 1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        data[5] = 0;
        data[6] = 0;
        data[7] = -149. / 60.;
        data[8] = 6.25;
        data[9] = -25. / 3.;
        data[10] = 25. / 3.;
        data[11] = -6.25;
        data[12] = 149. / 60.;
        data[13] = -1;
        data[14] = 1399. / 600.;
        data[15] = -445. / 48.;
        data[16] = 595. / 36.;
        data[17] = -215. / 12.;
        data[18] = 335. / 24.;
        data[19] = -20269. / 3600.;
        data[20] = 137. / 60.;
        data[21] = -13. / 12.;
        data[22] = 509. / 96.;
        data[23] = -67. / 6.;
        data[24] = 323. / 24.;
        data[25] = -133. / 12.;
        data[26] = 439. / 96.;
        data[27] = -1.875;
        data[28] = 4. / 15.;
        data[29] = -47. / 32.;
        data[30] = 31. / 9.;
        data[31] = -109. / 24.;
        data[32] = 4;
        data[33] = -2449. / 1440.;
        data[34] = 17. / 24.;
        data[35] = -1. / 30.;
        data[36] = 19. / 96.;
        data[37] = -0.5;
        data[38] = 17. / 24.;
        data[39] = -2. / 3.;
        data[40] = 47. / 160.;
        data[41] = -0.125;
        data[42] = 1. / 600.;
        data[43] = -1. / 96.;
        data[44] = 1. / 36.;
        data[45] = -1. / 24;
        data[46] = 1. / 24.;
        data[47] = -137. / 7200.;
        data[48] = 1. / 120.;
    }

    void IVanderExt::Derivate(DenseMatrix& M) const
    {
        for (int i = 0; i < m - 1; i++)
            for (int j = 0; j < m; j++)
                M(i, j) = this->operator()(i + 1, j) * (i + 1);
    }

    void IVanderExt::Interp(const SMultiVector& xn,
                            const Vector& dx,
                            SMultiVector& new_xn,
                            Vector& new_dx,
                            Real dt,
                            Real Dt) const
    {
        int n = xn.GetSSize();
#ifdef CHECK_MEM_OP
        if (n + 1 != m) {
            cerr << "The list of vector has incorrect size in IVanderExt[" << m << "]::Interp(xn["
                 << xn.GetSSize() << "])" << endl;
            abort();
        }
#endif
        new_xn.Resize(n, xn.GetXSize());
        SVector coeffs(n + 1);
        PVector subvec_xn, subvec_newxn;
        Real ratio = dt / Dt;

        new_xn.GetPVector(n - 1, subvec_newxn);
        xn.GetPVector(n - 1, subvec_xn);
        subvec_newxn.CopyData(subvec_xn);

        for (int i = 1; i < n; i++) {
            coeffs(0) = 1;
            for (int j = 1; j < n + 1; j++)
                coeffs(j) = coeffs(j - 1) * ratio * i;
            this->MatMult(coeffs, true);

            new_xn.GetPVector(n - 1 - i, subvec_newxn);
            for (int j = 0; j < n; j++) {
                xn.GetPVector(j, subvec_xn);
                subvec_newxn.AXPBY(coeffs(n - 1 - j), 1.0, subvec_xn);
            }
            subvec_newxn.AXPBY(Dt * coeffs(n), 1.0, dx);
        }

        coeffs(0) = 1;
        for (int j = 1; j < n + 1; j++)
            coeffs(j) = coeffs(j - 1) * ratio * (n - 1);
        DenseMatrix deriv(n + 1);
        this->Derivate(deriv);
        deriv.MatMult(coeffs, true);

        new_dx.Fill();
        for (int j = 0; j < n; j++) {
            xn.GetPVector(j, subvec_xn);
            new_dx.AXPBY(coeffs(n - 1 - j) / Dt, 1.0, subvec_xn);
        }
        new_dx.AXPBY(coeffs(n), 1.0, dx);
    }
}  // namespace paradae
}  // namespace griddyn
