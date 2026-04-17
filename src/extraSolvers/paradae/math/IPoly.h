/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../common/def.h"
#include "DenseMatrix.h"
#include "PMultiVector.h"
#include "Vector.h"

namespace griddyn::paradae {
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
}  // namespace griddyn::paradae
