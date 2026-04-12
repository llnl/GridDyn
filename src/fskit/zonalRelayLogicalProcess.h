/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * LLNS Copyright Start
 * Copyright (c) 2016, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */
#pragma once
#ifndef ZONAL_RELAY_LOGICAL_PROCESS_H
#    define ZONAL_RELAY_LOGICAL_PROCESS_H

#    include <fskit/event-message.h>
#    include <fskit/logical-process.h>
#    include <string>

class ZonalRelayLogicalProcess: public fskit::LogicalProcess {
  public:
    ZonalRelayLogicalProcess(const std::string& id);
    virtual ~ZonalRelayLogicalProcess();
    void ProcessEventMessage(const fskit::EventMessage& eventMessage);

  private:
};

#endif  // ZONAL_RELAY_LOGICAL_PROCESS_H
