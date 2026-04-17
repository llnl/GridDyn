/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "PMultiVector.h"

#include "SMultiVector.h"
#include "SVector.h"
#include <iostream>

namespace griddyn::paradae {
    using namespace std;

    PMultiVector::PMultiVector(const SMultiVector& v)
    {
        m = v.GetM();
        data = v.GetData();
        nx = v.GetXSize();
        ns = v.GetSSize();
    }

    PMultiVector::PMultiVector(const Vector& v, int ns_, int nx_)
    {
#ifdef CHECK_MEM_OP
        if (ns_ * nx_ != v.GetM()) {
            cerr << "Error in PMultiVector::PMultiVector(v[" << v.GetM() << "]," << ns_ << ","
                 << nx_ << ")" << endl;
            abort();
        }
#endif
        m = v.GetM();
        data = v.GetData();
        nx = nx_;
        ns = ns_;
    }

    PMultiVector& PMultiVector::operator=(const SMultiVector& v)
    {
        m = v.GetM();
        data = v.GetData();
        nx = v.GetXSize();
        ns = v.GetSSize();
        return *this;
    }

    PMultiVector& PMultiVector::operator=(const PMultiVector& v)
    {
        m = v.m;
        data = v.data;
        nx = v.nx;
        ns = v.ns;
        return *this;
    }

    void PMultiVector::GetPVector(int i, PVector& v) const
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= ns) {
            cerr << "Error in PMultiVector[" << ns << "," << nx << "]::GetPVector(" << i << ")"
                 << endl;
            abort();
        }
#endif
        v.Set(nx, data + i * nx);
    }

    void PMultiVector::GetSVector(int i, SVector& v) const
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= ns) {
            cerr << "Error in PMultiVector[" << ns << "," << nx << "]::GetSVector(" << i << ")"
                 << endl;
            abort();
        }
#endif
        v.Resize(nx);
        memcpy(v.GetData(), data + i * nx, nx * sizeof(Real));
    }
}  // namespace griddyn::paradae
