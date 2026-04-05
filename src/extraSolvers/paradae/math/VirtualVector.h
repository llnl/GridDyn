/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef VirtualVector_h
#define VirtualVector_h

#include "../common/def.h"

namespace griddyn {
namespace paradae {
    class Vector;

    class VirtualVector {
      public:
        virtual int GetM() const = 0;
        virtual Real* GetData() = 0;
        virtual ~VirtualVector() {};
        virtual void SetSubVec(int i, const Vector& vec) = 0;
        virtual Vector GetSubVec(int i, int l) const = 0;
    };
}  // namespace paradae
}  // namespace griddyn

#endif
