/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_MATH_IPOLY_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_MATH_IPOLY_H_

#include "../common/def.h"
#include "DenseMatrix.h"
#include "PMultiVector.h"
#include "Vector.h"

namespace griddyn {
namespace paradae {
    class IPoly {
        // y values and associated times
        PMultiVector tyy;
        PMultiVector yy;
        // yp values and associated times
        PMultiVector typ;
        PMultiVector yp;
        // Vandermonde Matrix
        DenseMatrix M;
        bool is_built;
        int nyy, nyp;
        int nx;
        Real min_time, max_time;

      public:
        IPoly(const PMultiVector& tyy_, const PMultiVector& yy_);
        IPoly(const PMultiVector& tyy_,
              const PMultiVector& yy_,
              const PMultiVector& typ_,
              const PMultiVector yp_);
        void Build();
        void GetValueY(Real t, Vector& y);
        void GetValueDY(Real t, Vector& yp);
        inline int GetXSize() { return nx; };
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_MATH_IPOLY_H_
