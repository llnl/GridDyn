/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstddef>
#include <string>
#include <toml11/types.hpp>

/** Internal value wrapper used by TomlReaderElement.
 *
 * It retains an ordered TOML value, its reader-visible name, and the indices
 * used while traversing table children and array entries.
 */
class TomlElement {
  public:
    /** Position of this value among its parent's table entries. */
    std::size_t mElementIndex = 0;

    /** Reader-visible name of this value. */
    std::string mName;

    /** Current item index when this value wraps an array. */
    std::size_t mArrayIndex = 0;

    /** Creates an empty wrapper. */
    TomlElement() = default;

    /** Wraps a TOML value and assigns its reader-visible name.
     * @param[in] vElement ordered TOML value to retain
     * @param[in] newName name exposed by the reader
     */
    TomlElement(toml::ordered_value vElement, std::string newName);

    /** Resets the wrapped value, name, and traversal indices. */
    void clear();

    /** Gets the selected array item, or the wrapped value when it is not an
     * array.
     */
    const toml::ordered_value& getElement() const;

    /** Gets the array length, or one for a non-array value. */
    std::size_t count() const;

    /** Checks whether the selected value is empty or unavailable. */
    bool isNull() const;

  private:
    toml::ordered_value mElement;
    bool mArrayType = false;
};
