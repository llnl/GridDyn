/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
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
