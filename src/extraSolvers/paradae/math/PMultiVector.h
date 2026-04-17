/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "PVector.h"

namespace griddyn::paradae {
    class SMultiVector;
    class SVector;

    class PMultiVector: public PVector {
        int nx;
        int ns;

      public:
        PMultiVector(): PVector(), nx(1), ns(0) {};
        PMultiVector(const SMultiVector& v);
        PMultiVector(const Vector& v, int ns_, int nx_);
        virtual ~PMultiVector() {};
        PMultiVector& operator=(const SMultiVector& v);
        PMultiVector& operator=(const PMultiVector& v);
        void GetPVector(int i, PVector& v) const;
        void GetSVector(int i, SVector& v) const;
        int GetXSize() const { return nx; };
        int GetSSize() const { return ns; };
    };
}  // namespace griddyn::paradae
