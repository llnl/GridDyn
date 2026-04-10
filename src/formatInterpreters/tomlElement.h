/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "toml/toml.h"
#include <string>

class tomlElement {
  public:
    int elementIndex = 0;
    std::string name;
    int arrayIndex = 0;
    tomlElement() = default;
    tomlElement(toml::Value vElement, std::string newName);

    void clear();
    const toml::Value& getElement() const
    {
        return (arraytype) ? *element.find(arrayIndex) : element;
    }
    int count() const { return (arraytype) ? static_cast<int>(element.size()) : 1; }
    bool isNull() const
    {
        return (arraytype) ? element.find(arrayIndex) != nullptr : element.empty();
    }

  private:
    toml::Value element;
    bool arraytype = false;
};
