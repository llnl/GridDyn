/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Event.h"
#include "gmlc/utilities/TimeSeriesMulti.hpp"
#include <future>
#include <memory>
#include <string>
#include <vector>
namespace griddyn {
/** namespace to contain different event types that can be used in the griddyn system*/
namespace events {
    /** event player allowing a timeSeries of events to occur over numerous time points on a single
     * object and field*/
    class Player: public Event {
      protected:
        // the bool is first to take advantage of the empty space in event there will still be a 3
        // byte gap
        bool loadFileProcess = false;  //!< flag indicating that the files need to be loaded yet
        coreTime period = maxTime;  //!< period of the player
        gmlc::utilities::TimeSeries<double, coreTime>
            ts;  //!< the time series containing the data for the player
        index_t currIndex = kNullLocation;  //!< the current index of the player
        index_t column = 0;  //!< the column in the file to use as the value set
        coreTime timeOffset = timeZero;  //!< an offset to the time series time
        std::string eFile;  //!< the file name
        std::future<void>
            fileLoaded;  //!< future indicating that the file operations have completed

      public:
        /** constructor taking the object name*/
        explicit Player(const std::string& eventName);
        /** constructor taking a trigger time and period default constructor*/
        Player(coreTime time0 = 0.0, double loopPeriod = 0.0);
        /** constructor from an event Info structure and rootobject*/
        Player(const EventInfo& gdEI, coreObject* rootObject);
        virtual std::unique_ptr<Event> clone() const override;

        virtual void cloneTo(Event* gE) const override;

        virtual void updateEvent(const EventInfo& gdEI, coreObject* rootObject) override;
        virtual change_code trigger() override;
        virtual change_code trigger(coreTime time) override;

        virtual void set(const std::string& param, double val) override;
        virtual void set(const std::string& param, const std::string& val) override;
        void setTime(coreTime time) override;
        /** set a time and value pair as part of the player
    @param[in] time the time associated with the value
    @param[in] val the value to set at the given time
    */
        void setTimeValue(coreTime time, double val);
        /** set a time and value set as part of the player
    @param[in] time the times associated with the value
    @param[in] val the values to set at the given times
    */
        void setTimeValue(const std::vector<coreTime>& time, const std::vector<double>& val);
        /** load a file containing the time/value data for the player
    @param[in] filename the file to load
    */
        void loadEventFile(const std::string& fileName);
        virtual std::string to_string() const override;

        virtual bool setTarget(coreObject* gdo, const std::string& var = "") override;

        virtual void initialize() override;
        // friendly helper functions for sorting
      protected:
        /** update the trigger time*/
        virtual void updateTrigger(coreTime time);
        /** queue up the next value to set when the event is triggered*/
        virtual void setNextValue();
    };
}  // namespace events
}  // namespace griddyn
