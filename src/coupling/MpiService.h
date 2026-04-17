/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/griddyn-config.h"
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace griddyn::mpi {
    /** service object to get a singular point for startup and shutdown of MPI*/
    class MpiService {
      public:
        /** helper class for ordering MPI calls*/
        class tokenholder {
          public:
            explicit tokenholder(std::unique_lock<std::mutex> lock, bool lockable = true):
                m_lock(std::move(lock)), needsLocking(lockable)
            {
            }
            /** release the token*/
            void release()
            {
                if (needsLocking) {
                    if (m_lock.owns_lock()) {
                        m_lock.unlock();
                    }
                }
            }
            /** reclaim the token*/
            void claim()
            {
                if (needsLocking) {
                    if (!m_lock.owns_lock()) {
                        m_lock.lock();
                    }
                }
            }

          private:
            std::unique_lock<std::mutex> m_lock;  //!< unique lock for managing the mutex
            bool needsLocking;  //!< flag indicating whether we even need to bother with locking
        };

      public:
        using token = std::unique_ptr<tokenholder>;
        /**
         * Create a new instance of the MpiService.  This should be
         * called at the program launch with the command line arguments
         * since this method invoke MPI_Thread_Init.
         *
         * Note that argc and argv may NOT be the same on return, just as in
         * MPI_Init.
         */
        static MpiService*
            instance(int* argc,
                     char** argv[],
                     int threadingLevel = 0);  // only constructor that creates Instance

        /**
         * Return the current instance of the singleton.
         */
        static MpiService* instance(int threadingLevel = 0);

        int getRank() const { return commRank; }
        int getCommSize() const { return commSize; }
        int getThreadingLevel() const { return mpiThreadingLevel; }

        /** get a token for executing MPI calls
    @throws runtime_error if a token cannot be obtained
    @return a token object. The token can be released or claimed
    */
        token getToken();

        ~MpiService();

      private:
        MpiService(int* argc, char** argv[], int threadingLevel);

        /**
         * Singleton instance.
         */
        static std::unique_ptr<MpiService> m_pInstance;
        static std::mutex startupLock;  //!< mutex protecting the instance
        MpiService(const MpiService&) = delete;
        MpiService& operator=(const MpiService&) = delete;

        int commRank = -1;  //!< the rank of the MPI instance
        int commSize = -1;  //!< total number of objects in the MPI comm
        int mpiThreadingLevel = -1;  //!< the threading level supported by the MPI library
        bool initialized_mpi = false;  //!< indicator that this instance started MPI
        std::thread::id
            tid;  //!< the current thread id (only used for MPI_THREAD_SINGLE or FUNNELED
        std::mutex tokenLock;  //!< mutex protecting MPI calls
    };

}  // namespace griddyn::mpi
