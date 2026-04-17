/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "MpiService.h"

#include <memory>
#include <mpi.h>
#include <utility>

namespace griddyn::mpi {
    std::unique_ptr<MpiService> MpiService::m_pInstance;
    std::mutex MpiService::startupLock;  //!< mutex protecting the instance

    MpiService* MpiService::instance(int* argc, char** argv[], int threadingLevel)
    {
        std::lock_guard<std::mutex> lock(startupLock);
        if (!m_pInstance) {
            m_pInstance = std::unique_ptr<MpiService>(new MpiService(argc, argv, threadingLevel));
        }
        return m_pInstance.get();
    }

    MpiService* MpiService::instance(int threadingLevel)
    {
        std::lock_guard<std::mutex> lock(startupLock);
        if (!m_pInstance) {
            m_pInstance =
                std::unique_ptr<MpiService>(new MpiService(nullptr, nullptr, threadingLevel));
        }
        return m_pInstance.get();
    }

    MpiService::MpiService(int* argc, char** argv[], int threadingLevel)
    {
        // initialize MPI with MPI_THREAD_FUNNELED
        int mpi_initialized;

        MPI_Initialized(&mpi_initialized);

        if (!mpi_initialized) {
            MPI_Init_thread(argc, argv, MPI_THREAD_MULTIPLE, &mpiThreadingLevel);

            MPI_Initialized(&mpi_initialized);
            if (!mpi_initialized) {
                throw(std::runtime_error("unable to initialize MPI"));
            }
            initialized_mpi = true;
        } else {
            MPI_Query_thread(&mpiThreadingLevel);
        }
        if (mpiThreadingLevel < threadingLevel) {
            throw(std::runtime_error("MPI not available for requested threading level"));
        }
        if (mpiThreadingLevel == MPI_THREAD_FUNNELED) {
            tid = std::this_thread::get_id();
        }
        // get number of tasks
        MPI_Comm_size(MPI_COMM_WORLD, &commSize);

        // get taskId of this task
        MPI_Comm_rank(MPI_COMM_WORLD, &commRank);
    }

    MpiService::~MpiService()
    {
        if (initialized_mpi) {
            // Finalize MPI
            int mpi_initialized;
            MPI_Initialized(&mpi_initialized);

            // Probably not a necessary check, a user using MPI should have also initialized it
            // themselves
            if (mpi_initialized) {
                //  std::cout << "About to finalize MPI for rank " << commRank << std::endl;
                MPI_Finalize();
                //  std::cout << "MPI Finalized for rank " << commRank << std::endl;
            }
        }
    }

    MpiService::token MpiService::getToken()
    {
        switch (mpiThreadingLevel) {
            case MPI_THREAD_MULTIPLE: {
                std::unique_lock<std::mutex> lock(tokenLock, std::defer_lock);
                return std::make_unique<tokenholder>(std::move(lock), false);
            } break;
            case MPI_THREAD_SERIALIZED:
            default: {
                std::unique_lock<std::mutex> lock(tokenLock);
                return std::make_unique<tokenholder>(std::move(lock));
            } break;
            case MPI_THREAD_FUNNELED:
            case MPI_THREAD_SINGLE:
                if (std::this_thread::get_id() == tid) {
                    std::unique_lock<std::mutex> lock(tokenLock);
                    return std::make_unique<tokenholder>(std::move(lock));
                } else {
                    throw(std::runtime_error("invalid thread for MPI calls"));
                }
                break;
        }
    }

}  // namespace griddyn::mpi
