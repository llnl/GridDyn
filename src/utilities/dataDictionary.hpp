/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "mapOps.hpp"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace utilities {
/** class to contain a dictionary in a thread safe manner
 */
template<typename keyType, typename dataType>
class dataDictionary {
  private:
    std::unordered_map<keyType, dataType> mValues;
    mutable std::mutex mDataLock;

  public:
    dataDictionary() = default;
    /** update the value corresponding to a key
    @param[in] key the lookup value
    @param[in] data the new value
    */
    void update(keyType key, const dataType& data)
    {
        std::lock_guard<std::mutex> updateLock(mDataLock);
        mValues[key] = data;
    }
    /** erase a value from the dictionary
    @param[in] key the lookup value to erase data for
    */
    void erase(keyType key)
    {
        std::lock_guard<std::mutex> updateLock(mDataLock);
        auto found = mValues.find(key);
        if (found != mValues.end()) {
            mValues.erase(found);
        }
    }
    /** copy a value from one key to another
    @param[in] origKey the key containing the source value
    @param[in] newKey the key to copy the value to
    */
    void copy(keyType origKey, keyType newKey)
    {
        std::lock_guard<std::mutex> updateLock(mDataLock);
        auto found = mValues.find(origKey);
        if (found != mValues.end()) {
            mValues[newKey] = found->second;
        }
    }
    /** thread safe function to get the values */
    dataType query(keyType key) const
    {
        std::lock_guard<std::mutex> updateLock(mDataLock);
        return mapFind(mValues, key, dataType());
    }
    /** same as query but not really threadsafe on read
    @details intended to be used if all the writes happen then just reads in a multithreaded context
    */
    dataType operator[](keyType key) const { return mapFind(mValues, key, dataType()); }
};

}  // namespace utilities
