/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace griddyn::paradae {
    using namespace std;

    void Vector::Fill(Real fill_)
    {
#ifdef CHECK_MEM_OP
        if (m <= 0) {
            cerr << "Error in  Vector[" << m << "]::Fill" << endl;
            abort();
        }
#endif
        fill(data, data + m, fill_);
    }

    Real Vector::operator()(int i) const
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= m) {
            cerr << "Error in Vector[" << m << "]::operator()(" << i << ")" << endl;
            abort();
        }
#endif
        return data[i];
    }

    Real& Vector::operator()(int i)
    {
#ifdef CHECK_MEM_OP
        if (i < 0 || i >= m) {
            cerr << "Error in Vector[" << m << "]::operator()(" << i << ")" << endl;
            abort();
        }
#endif
        return data[i];
    }

    Vector& Vector::operator+=(const Vector& v)
    {
#ifdef CHECK_MEM_OP
        if (m != v.m) {
            cerr << "Error in Vector[]" << m << "::operator+=(v[" << v.m << "])" << endl;
            abort();
        }
#endif
        for (int i = 0; i < m; i++)
            data[i] += v.data[i];
        return *this;
    }

    Vector& Vector::operator-=(const Vector& v)
    {
#ifdef CHECK_MEM_OP
        if (m != v.m) {
            cerr << "Error in Vector[]" << m << "::operator-=(v[" << v.m << "])" << endl;
            abort();
        }
#endif
        for (int i = 0; i < m; i++)
            data[i] -= v.data[i];
        return *this;
    }

    Vector& Vector::operator*=(Real alpha)
    {
        for (int i = 0; i < m; i++)
            data[i] *= alpha;
        return *this;
    }

    // y <- alpha*x+beta*y, y=this
    Vector& Vector::AXPBY(Real alpha, Real beta, const Vector& x)
    {
#ifdef CHECK_MEM_OP
        if (m != x.m) {
            cerr << "Error in Vector[]" << m << "::AXPBY(x[" << x.m << "])" << endl;
            abort();
        }
#endif
        for (int i = 0; i < m; i++)
            data[i] = alpha * x.data[i] + beta * data[i];
        return *this;
    }

    void Vector::CopyData(const Vector& v)
    {
#ifdef CHECK_MEM_OP
        if (m != v.GetM()) {
            cerr << "Error in Vector[" << m << "]::CopyData(v[" << v.GetM() << "])" << endl;
            abort();
        }
#endif
        memcpy(data, v.GetData(), m * sizeof(Real));
    }

    Real Vector::Norm2() const
    {
        Real res = 0;
        for (int i = 0; i < m; i++)
            res += data[i] * data[i];
        return sqrt(res);
    }

    Real Vector::NormInf() const
    {
        Real res = 0;
        for (int i = 0; i < m; i++)
            res = max(res, abs(data[i]));
        return res;
    }

    void Vector::dump() const
    {
        this->dump(cout);
    }

    void Vector::dump(ostream& output) const
    {
#ifdef CHECK_MEM_OP
        if (m <= 0) {
            cerr << "Error in Vector[" << m << "]::dump" << endl;
            abort();
        }
#endif
        output << data[0];
        for (int i = 1; i < m; i++)
            output << " " << data[i];
    }

    void Vector::dump(string filename) const
    {
        ofstream file;
        file.open(filename.c_str());
        file << setprecision(20);
        this->dump(file);
        file << endl;
        file.close();
    }

    ostream& operator<<(ostream& output, const Vector& vec)
    {
        vec.dump(output);
        return output;
    }
}  // namespace griddyn::paradae
