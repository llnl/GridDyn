/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "nlohmann/json.hpp"
#include <cstddef>
#include <string>

/** Internal value wrapper used by JsonReaderElement.
 *
 * It retains a JSON value, its reader-visible name, and the indices used while
 * traversing object children and array entries.
 */
class JsonElement {
  public:
    /** JSON value type used by this wrapper. */
    using JsonValue = nlohmann::json;

    /** Position of this value among its parent's object entries. */
    std::size_t mElementIndex = 0;

    /** Reader-visible name of this value. */
    std::string mName;

    /** Current item index when this value wraps an array. */
    std::size_t mArrayIndex = 0;

    /** Creates a null, unnamed wrapper. */
    JsonElement() noexcept {}

    /** Wraps a JSON value and assigns its reader-visible name.
     * @param[in] vElement JSON value to retain
     * @param[in] newName name exposed by the reader
     */
    JsonElement(JsonValue vElement, std::string newName);

    /** Resets the wrapped value, name, and traversal indices. */
    void clear();

    /** Gets the selected array item, or the wrapped value when it is not an
     * array.
     */
    const JsonValue& getElement() const { return (mArrayType) ? mElement[mArrayIndex] : mElement; }

    /** Gets the array length, or one for a non-array value. */
    std::size_t count() const { return (mArrayType) ? mElement.size() : std::size_t{1}; }

    /** Checks whether the selected value is JSON null. */
    bool isNull() const
    {
        return (mArrayType) ? mElement[mArrayIndex].is_null() : mElement.is_null();
    }

  private:
    JsonValue mElement = nullptr;
    bool mArrayType = false;
};
