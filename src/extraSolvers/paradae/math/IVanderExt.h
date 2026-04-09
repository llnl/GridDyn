/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_MATH_IVANDEREXT_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_MATH_IVANDEREXT_H_

#include "DenseMatrix.h"
#include "SMultiVector.h"
#include "Vector.h"

namespace griddyn {
namespace paradae {
    class IVanderExt: public DenseMatrix {
        void Build2();
        void Build3();
        void Build4();
        void Build5();
        void Build6();
        void Derivate(DenseMatrix& M) const;

      public:
        IVanderExt(int n);
        void Interp(const SMultiVector& xn,
                    const Vector& dx,
                    SMultiVector& new_xn,
                    Vector& new_dx,
                    Real dt,
                    Real Dt) const;
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_MATH_IVANDEREXT_H_
