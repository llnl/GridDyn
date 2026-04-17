/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
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
