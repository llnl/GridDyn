/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "jsonReaderElement.h"

#include "gmlc/utilities/stringConversion.h"
#include "jsonElement.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <print>
#include <sstream>
#include <string>
// default initialized empty string
static const char nullStr[] = "";

using gmlc::utilities::numeric_conversionComplete;

namespace {
using JsonValue = jsonElement::JsonValue;

bool isElement(const JsonValue& testValue);
bool isAttribute(const JsonValue& testValue);

bool isJsonNodeEmpty(const JsonValue& value)
{
    return value.is_null() || ((value.is_object() || value.is_array()) && value.empty());
}

std::string scalarToString(const JsonValue& value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<std::int64_t>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<std::uint64_t>());
    }
    if (value.is_number_float()) {
        return value.dump();
    }
    return nullStr;
}

bool parseJsonStream(std::istream& stream, JsonValue& output, std::string& errs)
{
    try {
        output = JsonValue::parse(stream);
        return true;
    }
    catch (const JsonValue::parse_error& err) {
        errs = err.what();
        return false;
    }
}
}  // namespace

jsonReaderElement::jsonReaderElement() = default;
jsonReaderElement::jsonReaderElement(const std::string& fileName)
{
    jsonReaderElement::loadFile(fileName);
}
void jsonReaderElement::clear()
{
    mParents.clear();
    if (mCurrent) {
        mCurrent->clear();
    }
}

bool jsonReaderElement::isValid() const
{
    return ((mCurrent) && (!mCurrent->isNull()));
}
bool jsonReaderElement::isDocument() const
{
    if (mParents.empty()) {
        if (mDoc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> jsonReaderElement::clone() const
{
    auto ret = std::make_shared<jsonReaderElement>();
    ret->mParents.reserve(mParents.size());
    for (const auto& parent : mParents) {
        ret->mParents.push_back(std::make_shared<jsonElement>(*parent));
    }
    ret->mCurrent = std::make_shared<jsonElement>(*mCurrent);
    ret->mDoc = mDoc;
    return ret;
}

bool jsonReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        mDoc = std::make_shared<JsonValue>();
        std::string errs;
        const bool parseOk = parseJsonStream(file, *mDoc, errs);
        if (parseOk) {
            mCurrent = std::make_shared<jsonElement>(*mDoc, fileName);
            return true;
        }

        std::println(stderr, "file read error in {}::{}", fileName, errs);
        mDoc = nullptr;
        clear();
        return false;
    }

    std::println(stderr, "unable to open file {}", fileName);
    mDoc = nullptr;
    clear();
    return false;
}

bool jsonReaderElement::parse(const std::string& inputString)
{
    std::ifstream file(inputString);
    mDoc = std::make_shared<JsonValue>();

    if (file.is_open()) {
        std::string errs;
        const bool parseOk = parseJsonStream(file, *mDoc, errs);
        if (!parseOk) {
            std::println(stderr, "Read error in stream::{}", errs);
            mDoc = nullptr;
            clear();
            return false;
        }
    } else {
        std::string errs;
        std::istringstream jstring(inputString);
        const bool parseOk = parseJsonStream(jstring, *mDoc, errs);
        if (!parseOk) {
            std::println(stderr, "Read error in stream::{}", errs);
            mDoc = nullptr;
            clear();
            return false;
        }
    }
    mCurrent = std::make_shared<jsonElement>(*mDoc, "");
    return true;
}

std::string jsonReaderElement::getName() const
{
    return mCurrent->mName;
}
double jsonReaderElement::getValue() const
{
    if (!isValid()) {
        return readerNullVal;
    }

    if (mCurrent->getElement().is_number()) {
        return mCurrent->getElement().get<double>();
    }
    if (mCurrent->getElement().is_string()) {
        return numeric_conversionComplete(mCurrent->getElement().get<std::string>(), readerNullVal);
    }
    return readerNullVal;
}

std::string jsonReaderElement::getText() const
{
    if (!isValid()) {
        return nullStr;
    }

    if (mCurrent->getElement().is_string()) {
        return mCurrent->getElement().get<std::string>();
    }
    return nullStr;
}

std::string jsonReaderElement::getMultiText(const std::string& /*sep*/) const
{
    if (!isValid()) {
        return nullStr;
    }

    if (mCurrent->getElement().is_string()) {
        return mCurrent->getElement().get<std::string>();
    }
    return nullStr;
}
// no attributes in json
bool jsonReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }

    if (mCurrent->getElement().is_object() && mCurrent->getElement().contains(attributeName)) {
        return (isAttribute(mCurrent->getElement()[attributeName]));
    }
    return false;
}

bool jsonReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    if (mCurrent->getElement().is_object() && mCurrent->getElement().contains(elementName)) {
        return (isElement(mCurrent->getElement()[elementName]));
    }

    return false;
}

readerAttribute jsonReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!mCurrent->getElement().is_object()) {
        return {};
    }

    auto attIterator = mCurrent->getElement().begin();
    auto elementEnd = mCurrent->getElement().end();
    mIteratorCount = 0;

    while (attIterator != elementEnd) {
        if (isAttribute(*attIterator)) {
            return {attIterator.key(), scalarToString(*attIterator)};
        }
        ++attIterator;
        ++mIteratorCount;
    }

    return {};
}

readerAttribute jsonReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!mCurrent->getElement().is_object()) {
        return {};
    }
    auto elementEnd = mCurrent->getElement().end();
    auto attIterator = mCurrent->getElement().begin();
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
        if (isAttribute(*attIterator)) {
            return {attIterator.key(), scalarToString(*attIterator)};
        }
        ++attIterator;
        ++mIteratorCount;
    }
    return {};
}

readerAttribute jsonReaderElement::getAttribute(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return {attributeName, scalarToString(mCurrent->getElement()[attributeName])};
    }
    return {};
}

std::string jsonReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return scalarToString(mCurrent->getElement()[attributeName]);
    }
    return nullStr;
}

double jsonReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        if (mCurrent->getElement()[attributeName].is_number()) {
            return mCurrent->getElement()[attributeName].get<double>();
        }
        return numeric_conversionComplete(scalarToString(mCurrent->getElement()[attributeName]),
                                          readerNullVal);
    }
    return readerNullVal;
}

std::shared_ptr<readerElement> jsonReaderElement::firstChild() const
{
    auto newElement = clone();
    newElement->moveToFirstChild();
    return newElement;
}

std::shared_ptr<readerElement> jsonReaderElement::firstChild(const std::string& childName) const
{
    auto newElement = clone();
    newElement->moveToFirstChild(childName);
    return newElement;
}

void jsonReaderElement::moveToFirstChild()
{
    if (!isValid()) {
        return;
    }
    mCurrent->mElementIndex = 0;
    auto elementIterator = mCurrent->getElement().begin();
    auto endIterator = mCurrent->getElement().end();

    while (elementIterator != endIterator) {
        if (isElement(*elementIterator)) {
            mParents.push_back(mCurrent);
            mCurrent = std::make_shared<jsonElement>(*elementIterator, elementIterator.key());
            return;
        }
        ++elementIterator;
        ++mCurrent->mElementIndex;
    }
    mParents.push_back(mCurrent);
    mCurrent->clear();
}

void jsonReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }

    if (mCurrent->getElement().contains(childName)) {
        if (isElement(mCurrent->getElement()[childName])) {
            mParents.push_back(mCurrent);
            mCurrent = std::make_shared<jsonElement>(mCurrent->getElement()[childName], childName);
            return;
        }
    }

    mParents.push_back(mCurrent);
    mCurrent->clear();
}

void jsonReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    ++mCurrent->mArrayIndex;
    while (mCurrent->mArrayIndex < mCurrent->count()) {
        if (!isJsonNodeEmpty(mCurrent->getElement())) {
            return;
        }
        ++mCurrent->mArrayIndex;
    }
    if (mParents.empty()) {
        mCurrent->clear();
        return;
    }
    // there are no more elements in a potential array
    auto elementIterator = mParents.back()->getElement().begin();
    auto endIterator = mParents.back()->getElement().end();
    ++mParents.back()->mElementIndex;
    // iterators don't survive copy so have to move the iterator to the next element index
    for (std::size_t ii = 0; ii < mParents.back()->mElementIndex; ++ii) {
        ++elementIterator;
        if (elementIterator == endIterator) {
            mCurrent->clear();
            return;
        }
    }
    // Now find the next valid element
    while (elementIterator != endIterator) {
        if (isElement(*elementIterator)) {
            mCurrent = std::make_shared<jsonElement>(*elementIterator, elementIterator.key());
            return;
        }
        ++elementIterator;
        ++mParents.back()->mElementIndex;
    }
    mCurrent->clear();
}

void jsonReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!isValid()) {
        return;
    }
    if (siblingName == mCurrent->mName) {
        ++mCurrent->mArrayIndex;
        while (mCurrent->mArrayIndex < mCurrent->count()) {
            if (!isJsonNodeEmpty(mCurrent->getElement())) {
                return;
            }
            ++mCurrent->mArrayIndex;
        }
        mCurrent->clear();
    } else {
        if (mParents.back()->getElement().contains(siblingName)) {
            if (isElement(mParents.back()->getElement()[siblingName])) {
                mCurrent = std::make_shared<jsonElement>(mParents.back()->getElement()[siblingName],
                                                        siblingName);
                return;
            }
        }
    }
}

void jsonReaderElement::moveToParent()
{
    if (mParents.empty()) {
        return;
    }
    mCurrent = mParents.back();
    mParents.pop_back();
}

std::shared_ptr<readerElement> jsonReaderElement::nextSibling() const
{
    auto newElement = clone();
    newElement->moveToNextSibling();
    return newElement;
}

std::shared_ptr<readerElement> jsonReaderElement::nextSibling(const std::string& siblingName) const
{
    auto newElement = clone();
    newElement->moveToNextSibling(siblingName);
    return newElement;
}

void jsonReaderElement::bookmark()
{
    mBookmarks.push_back(std::static_pointer_cast<jsonReaderElement>(clone()));
}

void jsonReaderElement::restore()
{
    if (mBookmarks.empty()) {
        return;
    }
    mParents = mBookmarks.back()->mParents;
    mCurrent = mBookmarks.back()->mCurrent;
    mBookmarks.pop_back();
}

namespace {

bool isAttribute(const JsonValue& testValue)
{
    if (testValue.is_object()) {
        return false;
    }
    if (testValue.is_array()) {
        return false;
    }
    return !testValue.is_null();
}

bool isElement(const JsonValue& testValue)
{
    if (testValue.is_null()) {
        return false;
    }

    if (testValue.is_object()) {
        return !testValue.empty();
    }
    if (testValue.is_array()) {
        return !testValue.empty();
    }

    return false;
}

}  // namespace
