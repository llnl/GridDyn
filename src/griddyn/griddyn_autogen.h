/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

#define AUTOGEN_GET /*autogen:get*/                                                                \
    virtual double get(std::string_view param, units::unit unitType = units::defunit) const;

#define AUTOGEN_GET_WITH_CUSTOM /*autogen:get_c*/                                                  \
    virtual double get(std::string_view param, units::unit unitType = units::defunit) const;     \
                                                                                                   \
  private:                                                                                         \
    double custom_get(std::string_view param, units::unit unitType) const;                       \
                                                                                                   \
  public:

#define AUTOGEN_SET /*autogen:set*/                                                                \
    virtual void set(std::string_view param, double val, units::unit unitType = units::defunit)  \
        override;

#define AUTOGEN_SET_WITH_CUSTOM /*autogen:set_c*/                                                  \
    virtual void set(std::string_view param, double val, units::unit unitType = units::defunit)  \
        override;                                                                                  \
                                                                                                   \
  private:                                                                                         \
    void custom_set(std::string_view param, double val, units::unit unitType);                   \
                                                                                                   \
  public:

#define AUTOGEN_SET_STRING /*autogen:setstring*/                                                   \
    virtual void set(std::string_view param, std::string_view val) override;

#define AUTOGEN_SET_STRING_WITH_CUSTOM /*autogen:setstring_c*/                                     \
    virtual void set(std::string_view param, std::string_view val) override;                   \
                                                                                                   \
  private:                                                                                         \
    void custom_set(std::string_view param, std::string_view val);                             \
                                                                                                   \
  public:

#define AUTOGEN_GET_STRING /*autogen:getstring*/                                                   \
    virtual std::string getString(std::string_view param) const override;

#define AUTOGEN_GET_STRING_WITH_CUSTOM /*autogen:getstring_c*/                                     \
    virtual std::string getString(std::string_view param) const override;                        \
                                                                                                   \
  private:                                                                                         \
    std::string custom_get(const std::string& param) const;                                        \
                                                                                                   \
  public:

#define AUTOGEN_GET_PSTRING /*autogen:getParameterString*/                                         \
    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;

#define AUTOGEN_SET_FLAG /*autogen:setflag*/                                                       \
    virtual void setFlag(std::string_view flag, bool val = true) override;

#define AUTOGEN_SET_FLAG_WITH_CUSTOM /*autogen:setflag_c*/                                         \
    virtual void setFlag(std::string_view flag, bool val = true) override;                       \
                                                                                                   \
  private:                                                                                         \
    void custom_setFlag(std::string_view flag, bool val);                                        \
                                                                                                   \
  public:

#define AUTOGEN_GET_FLAG /*autogen:getflag*/                                                       \
    virtual bool getFlag(std::string_view flag) const override;

#define AUTOGEN_GET_FLAG_WITH_CUSTOM /*autogen:getflag_c*/                                         \
    virtual bool getFlag(std::string_view flag) const override;                                  \
                                                                                                   \
  private:                                                                                         \
    bool custom_getFlag(std::string_view flag) const;                                            \
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
