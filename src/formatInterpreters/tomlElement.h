/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <toml.hpp>
#include <cstddef>
#include <string>

class tomlElement {
  public:
    int elementIndex = 0;
    std::string name;
    std::size_t arrayIndex = 0;
    tomlElement() = default;
    tomlElement(toml::ordered_value vElement, std::string newName);

    void clear();
    const toml::ordered_value& getElement() const;
    std::size_t count() const;
    bool isNull() const;

  private:
    toml::ordered_value element;
    bool arraytype = false;
};
