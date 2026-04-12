/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * LLNS Copyright Start
 * Copyright (c) 2017, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
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
