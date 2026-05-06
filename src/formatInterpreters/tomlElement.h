/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstddef>
#include <string>
#include <toml11/types.hpp>

class tomlElement {
  public:
    std::size_t elementIndex = 0;
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
