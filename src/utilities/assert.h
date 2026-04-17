/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#ifdef NDEBUG

#    define assert(condition) condition

#else
#    include <exception>
#    include <sstream>

class assert_exception: public std::exception {
  public:
    assert_exception(int line): m_line(line);
    virtual const char* what() const noexcept override
    {
        std::stringstream ss;
        ss << "assert() failed at line " << m_line << std::endl;
        return ss.str().c_str();
    }

  private:
    int m_line = -1;
}

#    define assert(condition)                                                                      \
        if (!condition) throw assert_exception(__LINE__)

#endif
