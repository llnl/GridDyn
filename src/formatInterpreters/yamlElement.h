/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "yaml-cpp/yaml.h"
#include <string>

class yamlElement {
  public:
    std::string mName;  //!< the name of the element
    std::size_t mElementIndex = 0;  //!< an indicator of element Index
    std::size_t mArrayIndex = 0;  //!< the current index into a sequence
    yamlElement() {}
    yamlElement(const YAML::Node& vElement, std::string newName);

    /** reset the element*/
    void clear();

    const YAML::Node getElement() const;
    size_t count() const { return (mArrayType) ? mElement.size() : ((mElement) ? 0 : 1); }
    bool isNull() const
    {
        if ((mElement) && (mElement.IsDefined())) {
            return (mArrayType) ? false : mElement.IsNull();
        }
        return true;
    }

  private:
    YAML::Node mElement;  //!< pointer to the actual YAML element
    bool mArrayType = false;  //!< indicator if the element is a sequence
};
