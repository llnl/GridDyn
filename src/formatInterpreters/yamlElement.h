/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "yaml-cpp/yaml.h"
#include <string>

/** Internal value wrapper used by YamlReaderElement.
 *
 * It retains a YAML node, its reader-visible name, and the indices used while
 * traversing mapping children and sequence entries.
 */
class YamlElement {
  public:
    /** Reader-visible name of this value. */
    std::string mName;

    /** Position of this value among its parent's mapping entries. */
    std::size_t mElementIndex = 0;

    /** Current item index when this value wraps a sequence. */
    std::size_t mArrayIndex = 0;

    /** Creates an undefined wrapper. */
    YamlElement() {}

    /** Wraps a YAML node and assigns its reader-visible name.
     * @param[in] vElement YAML node to retain
     * @param[in] newName name exposed by the reader
     */
    YamlElement(const YAML::Node& vElement, std::string newName);

    /** Resets the wrapped node, name, and traversal indices. */
    void clear();

    /** Gets the selected sequence item, or the wrapped node otherwise. */
    YAML::Node getElement() const;

    /** Gets the number of entries tracked for the wrapped value.
     * Sequence values report their sequence length.
     */
    size_t count() const { return (mArrayType) ? mElement.size() : ((mElement) ? 0 : 1); }

    /** Checks whether the selected YAML value is null or undefined. */
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
