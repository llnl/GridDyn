/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
// headers
// #include "gridDynSimulation.h"

#include "compoundEvent.h"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
namespace events {
    /** event type allowing multiple changes on multiple object at a set of given time points*/
    class compoundEventPlayer: public compoundEvent {
      protected:
        coreTime period = maxTime;  //!< period of the player
        gmlc::utilities::TimeSeriesMulti<double, coreTime>
            ts;  //!< the time series containing the data for the player
        index_t currIndex = kNullLocation;  //!< the current index of the player
        std::string eFile;  //!< the file name
        std::vector<index_t>
            columns;  //!< the columns of the time series to use for the different fields
      public:
        /** constructor from an event Name*/
        explicit compoundEventPlayer(const std::string& eventName);
        /** default constructor*/
        compoundEventPlayer();
        /** construct from an eventInfo structure and root object*/
        compoundEventPlayer(EventInfo& gdEI, coreObject* rootObject);
        virtual std::unique_ptr<Event> clone() const override;

        virtual void cloneTo(Event* gE) const override;

        // virtual void updateEvent(EventInfo &gdEI, coreObject *rootObject) override;

        virtual change_code trigger() override;
        virtual change_code trigger(coreTime time) override;

        virtual void set(const std::string& param, double val) override;
        virtual void set(const std::string& param, const std::string& val) override;
        void setTime(coreTime time) override;
        void setTimeValue(coreTime time, double val);
        void setTimeValue(const std::vector<coreTime>& time, const std::vector<double>& val);
        /** load the player data from a file
    @param[in] fileName the name of the file to load
    */
        void loadEventFile(const std::string& fileName);
        virtual std::string to_string() override;

        virtual bool setTarget(coreObject* gdo, const std::string& var = "") override;
        virtual void initialize() override;

      protected:
        /** helper function to update the trigger time*/
        virtual void updateTrigger(coreTime time);
    };
}  // namespace events
}  // namespace griddyn
