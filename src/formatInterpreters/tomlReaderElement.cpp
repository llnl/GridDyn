/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tomlReaderElement.h"

#include "gmlc/utilities/stringConversion.h"
#include "tomlElement.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <print>
#include <string>
#include <vector>

static const char nullStr[] = "";
using gmlc::utilities::numeric_conversionComplete;

namespace {
using TomlValue = toml::ordered_value;

bool isElement(const TomlValue& testValue);
bool isAttribute(const TomlValue& testValue);

bool isTomlNodeEmpty(const TomlValue& value)
{
    return value.is_empty() || ((value.is_table() || value.is_array()) && value.size() == 0);
}

std::string scalarToString(const TomlValue& value)
{
    if (value.is_string()) {
        return value.as_string();
    }
    if (value.is_boolean()) {
        return value.as_boolean() ? "true" : "false";
    }
    if (value.is_integer()) {
        return std::to_string(value.as_integer());
    }
    if (value.is_floating()) {
        return std::to_string(value.as_floating());
    }
    return nullStr;
}

std::string formatErrors(const std::vector<toml::error_info>& errors)
{
    std::string msg;
    for (const auto& err : errors) {
        msg += toml::format_error(err);
    }
    return msg;
}
}  // namespace

tomlReaderElement::tomlReaderElement() = default;
tomlReaderElement::tomlReaderElement(const std::string& fileName)
{
    tomlReaderElement::loadFile(fileName);
}

bool tomlReaderElement::isValid() const
{
    return ((mCurrent) && (!mCurrent->isNull()));
}
bool tomlReaderElement::isDocument() const
{
    if (mParents.empty()) {
        if (mDoc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> tomlReaderElement::clone() const
{
    auto ret = std::make_shared<tomlReaderElement>();
    ret->mParents.reserve(mParents.size());
    for (const auto& parent : mParents) {
        ret->mParents.push_back(std::make_shared<tomlElement>(*parent));
    }
    ret->mCurrent = std::make_shared<tomlElement>(*mCurrent);
    ret->mDoc = mDoc;
    return ret;
}

bool tomlReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName, std::ios::binary);
    if (file.is_open()) {
        auto parseResult = toml::try_parse<toml::ordered_type_config>(file, fileName);
        if (parseResult.is_ok()) {
            mDoc = std::make_shared<TomlValue>(parseResult.unwrap());
            mCurrent = std::make_shared<tomlElement>(*mDoc, fileName);
            return true;
        }

        std::println(stderr,
                     "file read error in {}::{}",
                     fileName,
                     formatErrors(parseResult.unwrap_err()));
        mDoc = nullptr;
        mParents.clear();
        if (mCurrent) {
            mCurrent->clear();
        }
        return false;
    }

    std::println(stderr, "unable to open file {}", fileName);
    mDoc = nullptr;
    mParents.clear();
    if (mCurrent) {
        mCurrent->clear();
    }
    return false;
}

bool tomlReaderElement::parse(const std::string& inputString)
{
    auto parseResult = toml::try_parse_str<toml::ordered_type_config>(inputString);
    if (parseResult.is_ok()) {
        mDoc = std::make_shared<TomlValue>(parseResult.unwrap());
        mCurrent = std::make_shared<tomlElement>(*mDoc, "string");
        return true;
    }

    std::println(stderr, "Read error in stream:: {}", formatErrors(parseResult.unwrap_err()));
    mDoc = nullptr;
    mParents.clear();
    if (mCurrent) {
        mCurrent->clear();
    }
    return false;
}

std::string tomlReaderElement::getName() const
{
    return mCurrent->mName;
}
double tomlReaderElement::getValue() const
{
    if (!isValid()) {
        return readerNullVal;
    }

    if (mCurrent->getElement().is_floating()) {
        return mCurrent->getElement().as_floating();
    }
    if (mCurrent->getElement().is_integer()) {
        return static_cast<double>(mCurrent->getElement().as_integer());
    }
    if (mCurrent->getElement().is_string()) {
        return numeric_conversionComplete(mCurrent->getElement().as_string(), readerNullVal);
    }
    return readerNullVal;
}

std::string tomlReaderElement::getText() const
{
    if (!isValid()) {
        return nullStr;
    }

    if (mCurrent->getElement().is_string()) {
        return mCurrent->getElement().as_string();
    }
    return nullStr;
}

std::string tomlReaderElement::getMultiText(const std::string& /*sep*/) const
{
    if (!isValid()) {
        return nullStr;
    }

    if (mCurrent->getElement().is_string()) {
        return mCurrent->getElement().as_string();
    }
    return nullStr;
}
// no attributes in toml
bool tomlReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }

    if (mCurrent->getElement().is_table() && mCurrent->getElement().contains(attributeName)) {
        return isAttribute(mCurrent->getElement().at(attributeName));
    }
    return false;
}

bool tomlReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    if (mCurrent->getElement().is_table() && mCurrent->getElement().contains(elementName)) {
        return isElement(mCurrent->getElement().at(elementName));
    }

    return false;
}

readerAttribute tomlReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!mCurrent->getElement().is_table()) {
        return {};
    }
    const auto& tab = mCurrent->getElement().as_table();
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    mIteratorCount = 0;

    while (attIterator != elementEnd) {
        if (isAttribute(attIterator->second)) {
            return {attIterator->first, scalarToString(attIterator->second)};
        }
        ++attIterator;
        ++mIteratorCount;
    }
    return {};
}

readerAttribute tomlReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!mCurrent->getElement().is_table()) {
        return {};
    }
    const auto& tab = mCurrent->getElement().as_table();
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    for (int ii = 0; ii < mIteratorCount; ++ii) {
        ++attIterator;
        if (attIterator == elementEnd) {
            return {};
        }
    }
    if (attIterator == elementEnd) {
        return {};
    }
    ++attIterator;
    ++mIteratorCount;
    while (attIterator != elementEnd) {
        if (isAttribute(attIterator->second)) {
            return {attIterator->first, scalarToString(attIterator->second)};
        }
        ++attIterator;
        ++mIteratorCount;
    }
    return {};
}

readerAttribute tomlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return {attributeName, scalarToString(mCurrent->getElement().at(attributeName))};
    }
    return {};
}

std::string tomlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return scalarToString(mCurrent->getElement().at(attributeName));
    }
    return nullStr;
}

double tomlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!hasAttribute(attributeName)) {
        return readerNullVal;
    }
    const auto& value = mCurrent->getElement().at(attributeName);
    if (value.is_floating()) {
        return value.as_floating();
    }
    if (value.is_integer()) {
        return static_cast<double>(value.as_integer());
    }
    return numeric_conversionComplete(scalarToString(value), readerNullVal);
}

std::shared_ptr<readerElement> tomlReaderElement::firstChild() const
{
    auto newElement = clone();
    newElement->moveToFirstChild();
    return newElement;
}

std::shared_ptr<readerElement> tomlReaderElement::firstChild(const std::string& childName) const
{
    auto newElement = clone();
    newElement->moveToFirstChild(childName);
    return newElement;
}

void tomlReaderElement::moveToFirstChild()
{
    if (!isValid()) {
        return;
    }
    mCurrent->mElementIndex = 0;
    if (mCurrent->getElement().is_table()) {
        const auto& tab = mCurrent->getElement().as_table();
        auto elementIterator = tab.begin();
        auto endIterator = tab.end();

        while (elementIterator != endIterator) {
            if (isElement(elementIterator->second)) {
                mParents.push_back(mCurrent);
                mCurrent =
                    std::make_shared<tomlElement>(elementIterator->second, elementIterator->first);
                return;
            }
            ++elementIterator;
            ++mCurrent->mElementIndex;
        }
    }
    mParents.push_back(mCurrent);
    mCurrent->clear();
}

void tomlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }
    if (!mCurrent->getElement().is_table()) {
        return;
    }
    if (mCurrent->getElement().contains(childName) &&
        isElement(mCurrent->getElement().at(childName))) {
        mParents.push_back(mCurrent);
        mCurrent = std::make_shared<tomlElement>(mCurrent->getElement().at(childName), childName);
        return;
    }

    mParents.push_back(mCurrent);
    mCurrent->clear();
}

void tomlReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    ++mCurrent->mArrayIndex;
    while (mCurrent->mArrayIndex < mCurrent->count()) {
        if (!isTomlNodeEmpty(mCurrent->getElement())) {
            return;
        }
        ++mCurrent->mArrayIndex;
    }
    if (mParents.empty()) {
        mCurrent->clear();
        return;
    }
    // there are no more elements in a potential array

    const auto& tab = mParents.back()->getElement().as_table();
    auto elementIterator = tab.begin();
    auto elementEnd = tab.end();
    ++mParents.back()->mElementIndex;
    // iterators don't survive copy so have to move the iterator to the next element index
    for (std::size_t ii = 0; ii < mParents.back()->mElementIndex; ++ii) {
        ++elementIterator;
        if (elementIterator == elementEnd) {
            mCurrent->clear();
            return;
        }
    }
    // Now find the next valid element
    while (elementIterator != elementEnd) {
        if (isElement(elementIterator->second)) {
            mCurrent =
                std::make_shared<tomlElement>(elementIterator->second, elementIterator->first);
            return;
        }
        ++elementIterator;
        ++mParents.back()->mElementIndex;
    }
    mCurrent->clear();
}

void tomlReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!isValid()) {
        return;
    }
    if (siblingName == mCurrent->mName) {
        ++mCurrent->mArrayIndex;
        while (mCurrent->mArrayIndex < mCurrent->count()) {
            if (!isTomlNodeEmpty(mCurrent->getElement())) {
                return;
            }
            ++mCurrent->mArrayIndex;
        }
        mCurrent->clear();
    } else {
        if (!mParents.empty() && mParents.back()->getElement().is_table() &&
            mParents.back()->getElement().contains(siblingName) &&
            isElement(mParents.back()->getElement().at(siblingName))) {
            mCurrent = std::make_shared<tomlElement>(mParents.back()->getElement().at(siblingName),
                                                     siblingName);
            return;
        }
    }
}

void tomlReaderElement::moveToParent()
{
    if (mParents.empty()) {
        return;
    }
    mCurrent = mParents.back();
    mParents.pop_back();
}

std::shared_ptr<readerElement> tomlReaderElement::nextSibling() const
{
    auto newElement = clone();
    newElement->moveToNextSibling();
    return newElement;
}

std::shared_ptr<readerElement> tomlReaderElement::nextSibling(const std::string& siblingName) const
{
    auto newElement = clone();
    newElement->moveToNextSibling(siblingName);
    return newElement;
}

void tomlReaderElement::bookmark()
{
    mBookmarks.push_back(std::static_pointer_cast<tomlReaderElement>(clone()));
}

void tomlReaderElement::restore()
{
    if (mBookmarks.empty()) {
        return;
    }
    mParents = mBookmarks.back()->mParents;
    mCurrent = mBookmarks.back()->mCurrent;
    mBookmarks.pop_back();
}

namespace {

bool isAttribute(const TomlValue& testValue)
{
    return !isTomlNodeEmpty(testValue) && !testValue.is_array() && !testValue.is_table();
}

bool isElement(const TomlValue& testValue)
{
    if (isTomlNodeEmpty(testValue)) {
        return false;
    }

    return testValue.is_array() || testValue.is_table();
}

}  // namespace
