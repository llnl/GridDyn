#pragma once

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

#include <string>

#define AUTOGEN_GET /*autogen:get*/                                                                \
    virtual double get(const std::string& param, units::unit unitType = units::defunit) const;

#define AUTOGEN_GET_WITH_CUSTOM /*autogen:get_c*/                                                  \
    virtual double get(const std::string& param, units::unit unitType = units::defunit) const;     \
                                                                                                   \
  private:                                                                                         \
    double custom_get(const std::string& param, units::unit unitType) const;                       \
                                                                                                   \
  public:

#define AUTOGEN_SET /*autogen:set*/                                                                \
    virtual void set(const std::string& param, double val, units::unit unitType = units::defunit)  \
        override;

#define AUTOGEN_SET_WITH_CUSTOM /*autogen:set_c*/                                                  \
    virtual void set(const std::string& param, double val, units::unit unitType = units::defunit)  \
        override;                                                                                  \
                                                                                                   \
  private:                                                                                         \
    void custom_set(const std::string& param, double val, units::unit unitType);                   \
                                                                                                   \
  public:

#define AUTOGEN_SET_STRING /*autogen:setstring*/                                                   \
    virtual void set(const std::string& param, const std::string& val) override;

#define AUTOGEN_SET_STRING_WITH_CUSTOM /*autogen:setstring_c*/                                     \
    virtual void set(const std::string& param, const std::string& val) override;                   \
                                                                                                   \
  private:                                                                                         \
    void custom_set(const std::string& param, const std::string& val);                             \
                                                                                                   \
  public:

#define AUTOGEN_GET_STRING /*autogen:getstring*/                                                   \
    virtual std::string getString(const std::string& param) const override;

#define AUTOGEN_GET_STRING_WITH_CUSTOM /*autogen:getstring_c*/                                     \
    virtual std::string getString(const std::string& param) const override;                        \
                                                                                                   \
  private:                                                                                         \
    std::string custom_get(const std::string& param) const;                                        \
                                                                                                   \
  public:

#define AUTOGEN_GET_PSTRING /*autogen:getParameterString*/                                         \
    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;

#define AUTOGEN_SET_FLAG /*autogen:setflag*/                                                       \
    virtual void setFlag(const std::string& flag, bool val = true) override;

#define AUTOGEN_SET_FLAG_WITH_CUSTOM /*autogen:setflag_c*/                                         \
    virtual void setFlag(const std::string& flag, bool val = true) override;                       \
                                                                                                   \
  private:                                                                                         \
    void custom_setFlag(const std::string& flag, bool val);                                        \
                                                                                                   \
  public:

#define AUTOGEN_GET_FLAG /*autogen:getflag*/                                                       \
    virtual bool getFlag(const std::string& flag) const override;

#define AUTOGEN_GET_FLAG_WITH_CUSTOM /*autogen:getflag_c*/                                         \
    virtual bool getFlag(const std::string& flag) const override;                                  \
                                                                                                   \
  private:                                                                                         \
    bool custom_getFlag(const std::string& flag) const;                                            \
                                                                                                   \
  public:

#define AUTOGEN_GET_SET                                                                            \
    AUTOGEN_SET                                                                                    \
    AUTOGEN_GET

#define AUTOGEN_GET_SET_CUSTOM                                                                     \
    AUTOGEN_SET_WITH_CUSTOM                                                                        \
    AUTOGEN_GET_WITH_CUSTOM

#define AUTOGEN_GET_SET_FLAGS                                                                      \
    AUTOGEN_SET_FLAG                                                                               \
    AUTOGEN_GET_FLAG

#define AUTOGEN_FLAGS_WITH_CUSTOM                                                                  \
    AUTOGEN_SET_FLAG_WITH_CUSTOM                                                                   \
    AUTOGEN_GET_FLAG_WITH_CUSTOM

#define AUTOGEN_GET_SET_STRING                                                                     \
    AUTOGEN_GET_STRING                                                                             \
    AUTOGEN_SET_STRING

#define AUTOGEN_GET_SET_STRING_WITH_CUSTOM                                                         \
    AUTOGEN_GET_STRING_WITH_CUSTOM                                                                 \
    AUTOGEN_SET_STRING_WITH_CUSTOM
