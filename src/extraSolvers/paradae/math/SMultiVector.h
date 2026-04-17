/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "SVector.h"

namespace griddyn::paradae {
class PVector;

class SMultiVector: public SVector {
    int nx;
    int ns;

  public:
    SMultiVector(): SVector(), nx(1), ns(0) {};
    SMultiVector(int ns_, int nx_, Real fill_ = 0);
    SMultiVector(const SMultiVector& mv);
    SMultiVector(const Vector& v);
    virtual ~SMultiVector() {};
    void Free();
    void Resize(int ns_, int nx_, Real fill_ = 0);
    SMultiVector& operator=(const SMultiVector& v);
    SMultiVector& operator=(const Vector& v);
    void GetPVector(int i, PVector& v) const;
    void GetSVector(int i, SVector& v) const;
    void PushFront(const SVector& v);
    void PushBack(const SVector& v);
    void PushBack(Real v);
    void PushBack(int v) { PushBack(Real(v)); };
    void PopFront(SVector& v);
    void PopBack(SVector& v);
    void PopFront();
    void PopBack();
    void PushAndPop(const SVector& v);
    void PushAndPop(Real v);
    void PushAndPop(int v) { PushAndPop(Real(v)); };
    int GetXSize() const { return nx; };
    int GetSSize() const { return ns; };
};
}  // namespace griddyn::paradae
