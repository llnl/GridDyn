/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "DenseMatrix.h"
#include "SMultiVector.h"
#include "Vector.h"

namespace griddyn::paradae {
    class IVander: public DenseMatrix {
        void Build2();
        void Build3();
        void Build4();
        void Build5();
        void Build6();
        void Derivate(DenseMatrix& M) const;

      public:
        IVander(int n);
        void Interp(const SMultiVector& xn,
                    const Vector& dx,
                    SMultiVector& new_xn,
                    Vector& new_dx,
                    Real dt,
                    Real Dt) const;
    };
}  // namespace griddyn::paradae
