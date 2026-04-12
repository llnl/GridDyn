/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "PVector.h"

#include "SVector.h"
#include <cstring>
#include <iostream>

namespace griddyn {
namespace paradae {
    using namespace std;

    PVector::PVector(const Vector& v)
    {
        m = v.GetM();
        data = v.GetData();
    }

    PVector& PVector::operator=(const Vector& v)
    {
        m = v.GetM();
        data = v.GetData();
        return *this;
    }

    PVector& PVector::operator=(const PVector& v)
    {
        m = v.m;
        data = v.data;
        return *this;
    }

    void PVector::Set(int m_, Real* data_)
    {
        m = m_;
        data = data_;
    }
}  // namespace paradae
}  // namespace griddyn
