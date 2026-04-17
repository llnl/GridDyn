/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gmlc/containers/mapOps.hpp"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>
/** creating a thread safe database object
@tparam keyType the lookup key type
@tparam dataType the type of data to store dataType must be copyable*/
template<typename keyType, typename dataType>
class dataDictionary {
  private:
    std::unordered_map<keyType, dataType> Vals;  //!< the actual value storage
    mutable std::mutex datalock;  //!< thread safe mechanism for storage
  public:
    dataDictionary() = default;
    /** update the value associated with a particular key
    @details if the key is not present it creates it, if it is it replaces it
    @param key the unique key value to update
    @param data the new data to store
    */
    void update(keyType key, const dataType& data)
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        Vals[key] = data;
    }
    /** update the value associated with a particular key
    @details if the key is not present it creates it, if it is it replaces it
    this overload uses move semantics
    @param key the unique key value to update
    @param data the new data to store
    */
    void update(keyType key, dataType&& data)
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        Vals[key] = std::move(data);
    }
    /** copy the value in one key to another
    @param origKey the key value to copy from
    @param newKey the key value to copy to*/
    void copy(keyType origKey, keyType newKey)
    {
        auto floc = Vals.find(origKey);
        if (floc != Vals.end()) {
            std::lock_guard<std::mutex> updateLock(datalock);
            Vals[newKey] = floc->second;
        }
    }
    /** get a copy of the data associated with a key
    @param key the key to query the value of
    @return a copy of the data associated with a key value
    */
    dataType query(keyType key) const
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        return mapFind(Vals, key, dataType());
    }

    /** remove a data entry from the dictionary
    @param key the key to remove*/
    void remove(keyType key)
    {
        std::lock_guard<std::mutex> updateLock(datalock);
        auto fnd = Vals.find(key);
        if (fnd != Vals.end) {
            Vals.erase(fnd);
        }
    }
};
