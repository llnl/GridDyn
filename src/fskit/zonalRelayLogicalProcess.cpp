/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zonalRelayLogicalProcess.h"

#include <iostream>
#include <string>

#define GRIDDYN_RANK 0

ZonalRelayLogicalProcess::ZonalRelayLogicalProcess(const std::string& id):
    LogicalProcess(fskit::GlobalLogicalProcessId(fskit::FederatedSimulatorId("gridDyn"),
                                                 GRIDDYN_RANK,
                                                 fskit::LocalLogicalProcessId(id)))
{
}

ZonalRelayLogicalProcess::~ZonalRelayLogicalProcess() = default;

void ZonalRelayLogicalProcess::ProcessEventMessage(const fskit::EventMessage& eventMessage)
{
    std::cout << "Received event message from ID " << eventMessage.GetGlobalLogicalProcessId()
              << std::endl;
}
