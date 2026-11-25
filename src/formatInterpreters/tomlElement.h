/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstddef>
#include <string>
#include <toml++/toml.hpp>

/** Internal value wrapper used by TomlReaderElement.
 *
 * It references a node in the reader's shared document and retains its
 * reader-visible name and traversal indices.
 */
class TomlElement {
  public:
    /** Position of the current child among this table's entries. */
    std::size_t mElementIndex = 0;

    /** Reader-visible name of this value. */
    std::string mName;

    /** Current item index when this value wraps an array. */
    std::size_t mArrayIndex = 0;

    /** Creates an empty wrapper. */
    TomlElement() = default;

    /** Wraps a TOML value and assigns its reader-visible name.
     * @param[in] vElement TOML node owned by the reader's shared document
     * @param[in] newName name exposed by the reader
     */
    TomlElement(const toml::node& vElement, std::string newName);

    /** Resets the wrapped value, name, and traversal indices. */
    void clear();

    /** Gets the selected array item, or the wrapped value when it is not an
     * array.
     */
    const toml::node& getElement() const;

    /** Gets the array length, or one for a non-array value. */
    std::size_t count() const;

    /** Checks whether the selected value is empty or unavailable. */
    bool isNull() const;

  private:
    const toml::node* mElement = nullptr;
    bool mArrayType = false;
};
