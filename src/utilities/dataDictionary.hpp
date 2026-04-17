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
    std::unordered_map<keyType, dataType> Vals;
    mutable std::mutex datalock;

  public:
    dataDictionary() = default;
    /** update the value corresponding to a key
    @param[in] key the lookup value
    @param[in] data the new value
    */
    void update(keyType key, const dataType& data)
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        Vals[key] = data;
    }
    /** erase a value from the dictionary
    @param[in] key the lookup value to erase data for
    */
    void erase(keyType key)
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        auto floc = Vals.find(origKey);
        if (floc != Vals.end()) {
            Vals.erase(floc);
        }
    }
    /** copy a value from one key to another
    @param[in] origKey the key containing the source value
    @param[in] newKey the key to copy the value to
    */
    void copy(keyType origKey, keyType newKey)
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        auto floc = Vals.find(origKey);
        if (floc != Vals.end()) {
            Vals[newKey] = floc->second;
        }
    }
    /** thread safe function to get the values */
    dataType query(keyType key) const
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        return mapFind(Vals, key, dataType());
    }
    /** same as query but not really threadsafe on read
    @details intended to be used if all the writes happen then just reads in a multithreaded context
    */
    dataType operator[](keyType key) const { return mapFind(Vals, key, dataType()); }
};

}  // namespace utilities
