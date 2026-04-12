/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SVector.h"

#include "PVector.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>

namespace griddyn {
namespace paradae {
    using namespace std;

    SVector::SVector(int m_, Real fill_)
    {
#ifdef CHECK_MEM_OP
        if (m_ < 0) {
            cerr << "Error in SVector::SVector(" << m_ << ")" << endl;
            abort();
        }
#endif
        m = m_;
        if (m > 0) {
            data = new Real[m];
            fill(data, data + m, fill_);
        } else
            data = nullptr;
    }

    SVector::SVector(const Vector& v)
    {
        m = v.GetM();
        data = nullptr;
        if (m > 0) {
            data = new Real[m];
            memcpy(data, v.GetData(), m * sizeof(Real));
        }
    }

    SVector::~SVector()
    {
        if (m > 0) delete[] data;
    }

    void SVector::Free()
    {
        if (m > 0) delete[] data;
        data = nullptr;
        m = 0;
    }

    void SVector::Resize(int m_, Real fill_)
    {
#ifdef CHECK_MEM_OP
        if (m_ <= 0) {
            cerr << "Error in SVector::SVector(" << m_ << ")" << endl;
            abort();
        }
#endif
        if (m != m_) {
            if (m > 0) delete[] data;
            m = m_;
            data = new Real[m];
        }
        fill(data, data + m, fill_);
    }

    void SVector::Append(Real alpha)
    {
        Real* new_data = new Real[m + 1];
        if (m > 0) {
            memcpy(new_data, data, m * sizeof(Real));
            delete[] data;
        }
        new_data[m] = alpha;
        data = new_data;
        m++;
    }

    bool SVector::operator==(const SVector& v) const
    {
        if (this == &v) return true;

        if (m != v.m) return false;

        bool ret = true;
        for (int i = 0; i < m; i++)
            ret = ret && data[i] == v.data[i];

        return ret;
    }

    SVector& SVector::operator=(const SVector& v)
    {
        if (this != &v) {
            if (m != v.m) {
                if (m > 0) delete[] data;
                m = v.m;
                if (m > 0) {
                    data = new Real[m];
                    memcpy(data, v.data, m * sizeof(Real));
                } else
                    data = nullptr;
            } else if (m > 0) {
                memcpy(data, v.data, m * sizeof(Real));
            }
        }
        return *this;
    }

    SVector& SVector::operator=(const PVector& v)
    {
        if (m != v.GetM()) {
            if (m > 0) delete[] data;
            m = v.GetM();
            if (m > 0) {
                data = new Real[m];
                memcpy(data, v.GetData(), m * sizeof(Real));
            } else
                data = nullptr;
        } else if (m > 0) {
            memcpy(data, v.GetData(), m * sizeof(Real));
        }
        return *this;
    }

    SVector SVector::Rand(int n, Real a, Real b)
    {
        SVector res(n);
        for (int i = 0; i < n; i++)
            res.data[i] = (b - a) * Real(rand()) / Real(RAND_MAX) + a;  // NOLINT
        return res;
    }
}  // namespace paradae
}  // namespace griddyn
