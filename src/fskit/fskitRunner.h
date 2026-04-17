/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#ifndef _FSKIT_RUNNER_H_
#    define _FSKIT_RUNNER_H_

#    include "fileInput/gridDynRunner.h"
#    include <memory>

namespace fskit {
class GrantedTimeWindowScheduler;
}

namespace griddyn {
class fskitRunner: public GriddynRunner {
  public:
    fskitRunner();

  private:
    using GriddynRunner::Initialize;

  public:
    int Initialize(int argc,
                   char* argv[],
                   std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler);
    virtual int Initialize(int argc, char* argv[]) override;

    virtual coreTime Run() override;
    virtual void Finalize() override;
};
}  // namespace griddyn
#endif
