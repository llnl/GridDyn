/*
Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle
Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for
Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence
Livermore National Security, LLC.
*/

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zmq {
class context_t;
}

namespace zmqlib {
/** class defining a singleton context manager for all zmq usage in gridDyn*/
class ZmqContextManager {
  private:
    static std::map<std::string, std::shared_ptr<ZmqContextManager>>
        contexts;  //!< container for pointers to all the available contexts
    std::string name;  //!< context name
    std::unique_ptr<zmq::context_t> zcontext;  //!< pointer to the actual context
    bool leakOnDelete = false;  //!< this is done to prevent some warning messages for use in DLL's
    ZmqContextManager(const std::string& contextName);

  public:
    static std::shared_ptr<ZmqContextManager>
        getContextPointer(const std::string& contextName = "");

    static zmq::context_t& getContext(const std::string& contextName = "");

    static void closeContext(const std::string& contextName = "");
    /** tell the context to free the pointer and leak the memory on delete
    @details You may ask why, well in windows systems when operating in a DLL if this context is
    closed after certain other operations that happen when the DLL is unlinked bad things can
    happen, and since in nearly all cases this happens at Shutdown leaking really doesn't matter
    that much
    @return true if the context was found and the flag set, false otherwise
    */
    static bool setContextToLeakOnDelete(const std::string& contextName = "");
    virtual ~ZmqContextManager();

    const std::string& getName() const { return name; }

    zmq::context_t& getBaseContext() const { return *zcontext; }
};

}  // namespace zmqlib
