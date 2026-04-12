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

#include <string>

#define AUTOGEN_SET /*autogen:set*/                                                                \
    virtual void set(const std::string& param,                                                     \
                     double val,                                                                   \
                     gridUnits::units_t unitType = gridUnits::defUnit) override;

#define AUTOGEN_SET_WITH_CUSTOM /*autogen:set_c*/                                                  \
    virtual void set(const std::string& param,                                                     \
                     double val,                                                                   \
                     gridUnits::units_t unitType = gridUnits::defUnit) override;                   \
                                                                                                   \
  private:                                                                                         \
    void custom_set(const std::string& param, double val, gridUnits::units_t unitType);            \
                                                                                                   \
  public:

#define AUTOGEN_GET /*autogen:get*/                                                                \
    virtual double get(const std::string& param, gridUnits::units_t unitType = gridUnits::defUnit) \
        const;

#define AUTOGEN_GET_WITH_CUSTOM /*autogen:get_c*/                                                  \
    virtual double get(const std::string& param, gridUnits::units_t unitType = gridUnits::defUnit) \
        const;                                                                                     \
                                                                                                   \
  private:                                                                                         \
    double custom_get(const std::string& param, gridUnits::units_t unitType) const;                \
                                                                                                   \
  public:

#define AUTOGEN_GET_SET                                                                            \
    AUTOGEN_SET                                                                                    \
    AUTOGEN_GET

#define AUTOGEN_GET_SET_CUSTOM                                                                     \
    AUTOGEN_SET_WITH_CUSTOM                                                                        \
    AUTOGEN_GET_WITH_CUSTOM

#define AUTOGEN_SET_STRING /*autogen:setstring*/                                                   \
    virtual void set(const std::string& param, const std::string& val) override;

#define AUTOGEN_SET_STRING_WITH_CUSTOM /*autogen:setstring_c*/                                     \
    virtual void set(const std::string& param, const std::string& val) override;                   \
                                                                                                   \
  private:                                                                                         \
    double custom_set(const std::string& param, const std::string& val);                           \
                                                                                                   \
  public:

#define AUTOGEN_SET_FLAG /*autogen:setflag*/                                                       \
    virtual void setFlag(const std::string& flag, bool val = true) override;

#define AUTOGEN_SET_FLAG_WITH_CUSTOM /*autogen:setflag_c*/                                         \
    virtual void setFlag(const std::string& flag, bool val = true) override;                       \
                                                                                                   \
  private:                                                                                         \
    double custom_setFlag(const std::string& flag, bool val);                                      \
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

#define AUTOGEN_FLAGS                                                                              \
    AUTOGEN_SET_FLAG                                                                               \
    AUTOGEN_GET_FLAG
