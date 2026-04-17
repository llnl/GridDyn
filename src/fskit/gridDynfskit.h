/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
/** @brief load the fskit components specified by the subset indicator into the object factory
@param[in] subset specify a subset of the models to load
"all" or use default for all models
others will be added as the library grows
*/
void loadFskit(const std::string& subset = "");
