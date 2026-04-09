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

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_INTERNAL_GRIDDYN_EXPORT_INTERNAL_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_INTERNAL_GRIDDYN_EXPORT_INTERNAL_H_
#include "gmlc/concurrency/TripWire.hpp"
#include "gmlc/libguarded/guarded.hpp"
#include "griddyn/gridComponent.h"
#include "griddyn/solvers/solverMode.hpp"
#include <deque>
#include <memory>
#include <string>
#include <vector>

typedef void* GridDynObject;
/** make a GridDynObject wrapper out of an actual component pointer*/
GridDynObject createGridDynObject(griddyn::gridComponent* comp);
/** get the component pointer from a GridDynObject*/
griddyn::gridComponent* getComponentPointer(GridDynObject obj);
/** get the const component pointer from a const GridDynObject*/
const griddyn::gridComponent* getConstComponentPointer(GridDynObject obj);

/** data class for storing some solver information and data buffers*/
class solverKeyInfo {
  public:
    griddyn::solverMode sMode_;  //!< solverMode
    std::vector<double> stateBuffer;  //!< buffer for storing state data
    std::vector<double> dstateBuffer;  //!< buffer for storing dstate_dt data
    std::vector<std::string> stateNames;  //!< buffer for storing the stateNames
    /** default constructor*/
    solverKeyInfo() = default;
    /** constructor from a solverMode reference*/
    solverKeyInfo(const griddyn::solverMode& sMode): sMode_(sMode) {}
};

/** allocate buffers for using a solverKeyInfo object with a gridComponent*/
void setUpSolverKeyInfo(solverKeyInfo* key, griddyn::gridComponent* comp);
/** translate a system state vector to a local state vector*/
void TranslateToLocal(const std::vector<double>& orig,
                      double* newData,
                      const griddyn::gridComponent* comp,
                      const griddyn::solverMode& sMode);
/** translate a local state vector into the appropriate elements of a system state vector*/
void CopyFromLocal(std::vector<double>& dest,
                   const double* localData,
                   const griddyn::gridComponent* comp,
                   const griddyn::solverMode& sMode);

/** definitions to simplify error returns if an error already exists*/
#define GRIDDYN_ERROR_CHECK(err, retval)                                                           \
    do {                                                                                           \
        if (((err) != nullptr) && ((err)->error_code != 0)) {                                      \
            return (retval);                                                                       \
        }                                                                                          \
    } while (false)

/** assign and error string and code to an error object if it exists*/
inline void assignError(GridDynError* err, int errorCode, const char* string)
{
    if (err != nullptr) {
        err->error_code = errorCode;
        err->message = string;
    }
}

extern const char emptyStr[];
extern const std::string nullStringArgument;
#define AS_STRING(str) ((str) != nullptr) ? std::string(str) : std::string(emptyStr)

#define CHECK_NULL_STRING(str, retval)                                                             \
    do {                                                                                           \
        if ((str) == nullptr) {                                                                    \
            if (err != nullptr) {                                                                  \
                err->error_code = helics_error_invalid_argument;                                   \
                err->message = nullStringArgument.c_str();                                         \
            }                                                                                      \
            return (retval);                                                                       \
        }                                                                                          \
    } while (false)

/**centralized error handler for the C interface*/
void griddynErrorHandler(GridDynError* err) noexcept;

/** class for containing all the objects associated with a federation*/
class MasterObjectHolder {
  private:
    gmlc::libguarded::guarded<std::deque<std::string>>
        errorStrings;  //!< container for strings generated from error conditions
  public:
    MasterObjectHolder() noexcept;
    ~MasterObjectHolder();
    /** store an error string to a string buffer
    @return a pointer to the memory location*/
    const char* addErrorString(std::string newError);
};

std::shared_ptr<MasterObjectHolder> getMasterHolder();
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_INTERNAL_GRIDDYN_EXPORT_INTERNAL_H_
