/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SMultiVector.h"

#include "PVector.h"
#include <cstring>
#include <iostream>

namespace griddyn {
namespace paradae {
    using namespace std;

    SMultiVector::SMultiVector(int ns_, int nx_, Real fill_):
        SVector(ns_ * nx_, fill_), nx(nx_), ns(ns_)
    {
#ifdef CHECK_MEM_OP
        if (ns < 1 || nx <= 0) {
            cerr << "Error in SMultiVector::SMultiVector(" << ns << "," << nx << ")" << endl;
            abort();
        }
#endif
    }

    SMultiVector::SMultiVector(const SMultiVector& mv): nx(mv.nx), ns(mv.ns)
    {
#ifdef CHECK_MEM_OP
        if (nx <= 0) {
            cerr << "Error in SMultiVector::SMultiVector(mv[" << ns << "," << nx << "])" << endl;
            abort();
        }
#endif
        m = nx * ns;
        if (ns > 0) {
            data = new Real[m];
            memcpy(data, mv.data, m * sizeof(Real));
        } else
            data = nullptr;
    }

    SMultiVector::SMultiVector(const Vector& v): nx(v.GetM()), ns(1)
    {
#ifdef CHECK_MEM_OP
        if (nx <= 0) {
            cerr << "Error in SMultiVector::SMultiVector(v[" << nx << "])" << endl;
            abort();
        }
#endif
        m = nx * ns;
        if (ns > 0) {
            data = new Real[m];
            memcpy(data, v.GetData(), m * sizeof(Real));
        } else
            data = nullptr;
    }

    void SMultiVector::Free()
    {
        SVector::Free();
        ns = 0;
    }

    void SMultiVector::Resize(int ns_, int nx_, Real fill_)
    {
#ifdef CHECK_MEM_OP
        if (ns_ < 1 || nx_ <= 0) {
            cerr << "Error in SMultiVector::Resize(" << ns << "," << nx << ")" << endl;
            abort();
        }
#endif
        SVector::Resize(ns_ * nx_, fill_);
        ns = ns_;
        nx = nx_;
    }

    SMultiVector& SMultiVector::operator=(const SMultiVector& v)
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
            nx = v.nx;
            ns = v.ns;
        }
        return *this;
    }

    SMultiVector& SMultiVector::operator=(const Vector& v)
    {
        if (this != &v) {
            const SMultiVector* mv = dynamic_cast<const SMultiVector*>(&v);
            if (mv != nullptr) {
                this->operator=(*mv);
            } else {
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
                if (m > 0) {
                    nx = m;
                    ns = 1;
                } else {
                    nx = 1;
                    ns = 0;
                }
            }
        }
        return *this;
    }

    void SMultiVector::GetPVector(int i, PVector& v) const
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= ns) {
            cerr << "Error in SMultiVector[" << ns << "," << nx << "]::GetPVector(" << i << ")"
                 << endl;
            abort();
        }
#endif
        v.Set(nx, data + i * nx);
    }

    void SMultiVector::GetSVector(int i, SVector& v) const
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= ns) {
            cerr << "Error in SMultiVector[" << ns << "," << nx << "]::GetSVector(" << i << ")"
                 << endl;
            abort();
        }
#endif
        v.Resize(nx);
        memcpy(v.GetData(), data + i * nx, nx * sizeof(Real));
    }

    void SMultiVector::PushFront(const SVector& v)
    {
#ifdef CHECK_MEM_OP
        if (nx != v.GetM()) {
            cerr << "Error in SMultiVector[" << ns << "," << nx << "]::PushFront(v[" << v.GetM()
                 << "])" << endl;
            abort();
        }
#endif
        Real* new_data = new Real[m + nx];
        memcpy(new_data, v.GetData(), nx * sizeof(Real));
        if (ns > 0) {
            memcpy(new_data + nx, data, m * sizeof(Real));
            delete[] data;
        }
        data = new_data;
        ns++;
        m += nx;
    }

    void SMultiVector::PushBack(const SVector& v)
    {
#ifdef CHECK_MEM_OP
        if (nx != v.GetM()) {
            cerr << "Error in SMultiVector[" << ns << "," << nx << "]::PushFront(v[" << v.GetM()
                 << "])" << endl;
            abort();
        }
#endif
        Real* new_data = new Real[m + nx];
        if (ns > 0) {
            memcpy(new_data, data, m * sizeof(Real));
            delete[] data;
        }
        memcpy(new_data + m, v.GetData(), nx * sizeof(Real));
        data = new_data;
        ns++;
        m += nx;
    }

    void SMultiVector::PushBack(Real v)
    {
#ifdef CHECK_MEM_OP
        if (nx != 1) {
            cerr << "Error in SMultiVector[" << ns << "," << nx << "]::PushFront(alpha)" << endl;
            abort();
        }
#endif
        Real* new_data = new Real[m + nx];
        if (ns > 0) {
            memcpy(new_data, data, m * sizeof(Real));
            delete[] data;
        }
        new_data[m] = v;
        data = new_data;
        ns++;
        m += nx;
    }

    void SMultiVector::PopFront(SVector& v)
    {
#ifdef CHECK_MEM_OP
        if (ns == 0) {
            cerr << "Error in SMultiVector[0]::PopBack(v)" << endl;
            abort();
        }
#endif
        v.Resize(nx);
        memcpy(v.GetData(), data, nx * sizeof(Real));
        this->PopFront();
    }

    void SMultiVector::PopBack(SVector& v)
    {
#ifdef CHECK_MEM_OP
        if (ns == 0) {
            cerr << "Error in SMultiVector[0]::PopBack(v)" << endl;
            abort();
        }
#endif
        v.Resize(nx);
        memcpy(v.GetData(), data + nx * (ns - 1), nx * sizeof(Real));
        this->PopBack();
    }

    void SMultiVector::PopFront()
    {
#ifdef CHECK_MEM_OP
        if (ns == 0) {
            cerr << "Error in SMultiVector[0]::PopBack()" << endl;
            abort();
        }
#endif
        if (ns == 1) {
            delete[] data;
            data = nullptr;
            ns = 0;
            m = 0;
        } else {
            Real* new_data = new Real[m - nx];
            memcpy(new_data, data + nx, (m - nx) * sizeof(Real));
            delete[] data;
            data = new_data;
            ns--;
            m -= nx;
        }
    }

    void SMultiVector::PopBack()
    {
#ifdef CHECK_MEM_OP
        if (ns == 0) {
            cerr << "Error in SMultiVector[0]::PopBack()" << endl;
            abort();
        }
#endif
        if (ns == 1) {
            delete[] data;
            data = nullptr;
            ns = 0;
            m = 0;
        } else {
            Real* new_data = new Real[m - nx];
            memcpy(new_data, data, (m - nx) * sizeof(Real));
            delete[] data;
            data = new_data;
            ns--;
            m -= nx;
        }
    }

    void SMultiVector::PushAndPop(const SVector& v)
    {
#ifdef CHECK_MEM_OP
        if (ns == 0 || nx != v.GetM()) {
            cerr << "Error in SMultiVector[" << ns << "," << nx << "]::PushAndPop(v[" << v.GetM()
                 << "])" << endl;
            abort();
        }
#endif
        if (ns > 1) memmove(data + nx, data, (m - nx) * sizeof(Real));
        memcpy(data, v.GetData(), nx * sizeof(Real));
    }

    void SMultiVector::PushAndPop(Real v)
    {
#ifdef CHECK_MEM_OP
        if (ns == 0 || nx != 1) {
            cerr << "Error in SMultiVector[" << ns << "," << nx << "]::PushAndPop(alpha)" << endl;
            abort();
        }
#endif
        if (ns > 1) memmove(data + nx, data, (m - nx) * sizeof(Real));
        data[0] = v;
    }

}  // namespace paradae
}  // namespace griddyn
