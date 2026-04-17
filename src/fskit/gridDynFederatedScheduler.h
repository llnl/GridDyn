/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cassert>
#include <memory>

namespace fskit {
class GrantedTimeWindowScheduler;
}

namespace griddyn {
class fskitRunner;
}  // namespace griddyn

/**
 * Singleton for accessing federated scheduler.
 *
 * Singleton is initialized in GriddynRunner.
 */
class GriddynFederatedScheduler {
  public:
    static bool IsFederated() { return g_scheduler != nullptr; }

    /**
     * Return singleton federated scheduler.
     */
    static std::shared_ptr<fskit::GrantedTimeWindowScheduler> GetScheduler()
    {
        assert(g_scheduler);
        return g_scheduler;
    }

    /* Make non-copyable since it is this class is used to access a singleton
     * via static methods.
     */
    GriddynFederatedScheduler() = default;
    GriddynFederatedScheduler(const GriddynFederatedScheduler&) = delete;
    GriddynFederatedScheduler& operator=(const GriddynFederatedScheduler&) = delete;

    /*
     * GriddynRunner initializes the federated scheduler.
     */
    friend class griddyn::fskitRunner;

  private:
    /**
     * Initialize singleton.
     */
    static void Initialize(std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler)
    {
        // TODO(pt): make this thread safe probably with a mutex lock
        g_scheduler = scheduler;
    }

    static std::shared_ptr<fskit::GrantedTimeWindowScheduler> g_scheduler;
};
