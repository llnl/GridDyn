/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "readerElement.h"

#include "gmlc/utilities/stringConversion.h"
#include <string>
#include <utility>

using gmlc::utilities::numeric_conversion;

ReaderAttribute::ReaderAttribute() = default;
ReaderAttribute::ReaderAttribute(std::string attName, std::string attText):
    mName(std::move(attName)), mText(std::move(attText))
{
}
ReaderAttribute::ReaderAttribute(std::string_view attName, std::string_view attText):
    mName(attName), mText(attText)
{
}
void ReaderAttribute::set(std::string_view attName, std::string_view attText)
{
    mName = attName;
    mText = attText;
}

double ReaderAttribute::getValue() const
{
    return numeric_conversion<double>(mText, readerNullVal);
}
constexpr int64_t nullLong = static_cast<int64_t>(0x8000'0000'0000'0000);
int64_t ReaderAttribute::getInt() const
{
    return numeric_conversion<int64_t>(mText, nullLong);
}
ReaderElement::~ReaderElement() = default;
