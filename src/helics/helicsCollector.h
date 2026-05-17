/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../griddyn/measurement/collector.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::helicsLib {
class HelicsCoordinator;

class HelicsCollector: public collector {
  private:
    class PublicationInfo {
      public:
        std::string columnName;
        int32_t publicationIndex = -1;
        bool vectorPublication = false;
        PublicationInfo() = default;
        PublicationInfo(const std::string& name, int32_t index, bool isVectorPublication = false):
            columnName(name), publicationIndex(index), vectorPublication(isVectorPublication)
        {
        }
    };
    enum class CollectorPubType {
        AS_INDIVIDUAL = 0,
        AS_VECTOR = 1,
        AS_STRING = 2,
    };
    CollectorPubType publicationType = CollectorPubType::AS_INDIVIDUAL;
    std::vector<std::pair<std::string, std::string>> complexPairs;
    std::vector<std::string> complexNames;
    std::vector<PublicationInfo> publications;
    std::string publicationName;
    int32_t multiPublicationIndex = -1;
    HelicsCoordinator* coordinator_ =
        nullptr;  //!< the coordinator for interaction with the helics interface
  public:
    HelicsCollector(coreTime time0 = timeZero, coreTime period = timeOneSecond);
    explicit HelicsCollector(const std::string& name);

    virtual std::unique_ptr<collector> clone() const override;
    virtual void cloneTo(collector* col) const override;
    virtual change_code trigger(coreTime time) override;

    void set(std::string_view param, double val) override;
    void set(std::string_view param, std::string_view val) override;

    virtual const std::string& getSinkName() const override;

  protected:
    void dataPointAdded(const collectorPoint& cp) override;

  private:
    /** function to find the fmi coordinator so we can connect to that*/
    void findCoordinator();
};

}  // namespace griddyn::helicsLib
