/*
Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle
Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for
Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence
Livermore National Security, LLC.
*/
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
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

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_NETWORKING_ZMQLIBRARY_ZMQHELPER_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_NETWORKING_ZMQLIBRARY_ZMQHELPER_H_

#include <string>

namespace zmq {
enum class socket_type : int;
}

namespace zmqlib {
zmq::socket_type socketTypeFromString(const std::string& socketType);
}
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_NETWORKING_ZMQLIBRARY_ZMQHELPER_H_
