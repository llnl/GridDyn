/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "VirtualMatrix.h"

#include <string>

namespace griddyn {
namespace paradae {
    std::ostream& operator<<(std::ostream& output, const VirtualMatrix& mat)
    {
        mat.dump(output);
        return output;
    }
    void VirtualMatrix::dump(std::string filename) const
    {
        std::ofstream file;
        file.open(filename.c_str());
        file << std::setprecision(20);
        this->dump(file);
        file.close();
    }
}  // namespace paradae
}  // namespace griddyn
