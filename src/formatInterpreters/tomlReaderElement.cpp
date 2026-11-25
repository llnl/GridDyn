/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tomlReaderElement.h"

#include "gmlc/utilities/stringConversion.h"
#include "tomlElement.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <print>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static constexpr char nullStr[] = "";
using gmlc::utilities::numeric_conversionComplete;

namespace {
using TomlValue = toml::node;
using TableEntry = std::pair<toml::key, const toml::node*>;

bool isElement(const TomlValue& testValue);
bool isAttribute(const TomlValue& testValue);

bool isTomlNodeEmpty(const TomlValue& value)
{
    if (const auto* table = value.as_table()) {
        return table->empty();
    }
    if (const auto* array = value.as_array()) {
        return array->empty();
    }
    return false;
}

std::string scalarToString(const TomlValue& value)
{
    if (value.is_string()) {
        return value.as_string()->get();
    }
    if (value.is_boolean()) {
        return value.as_boolean()->get() ? "true" : "false";
    }
    if (value.is_integer()) {
        return std::to_string(value.as_integer()->get());
    }
    if (value.is_floating_point()) {
        return std::to_string(value.as_floating_point()->get());
    }
    return nullStr;
}

std::string formatError(const toml::parse_error& error)
{
    std::ostringstream stream;
    stream << error;
    return stream.str();
}

std::vector<TableEntry> orderedEntries(const toml::table& table)
{
    std::vector<TableEntry> entries;
    entries.reserve(table.size());
    for (const auto& [key, value] : table) {
        entries.emplace_back(key, &value);
    }
    // tomlplusplus stores tables in key order. Source positions preserve the
    // input order expected by the reader interface, including implicit tables.
    std::stable_sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first.source().begin < rhs.first.source().begin;
    });
    return entries;
}

const toml::node* findEntry(const TomlValue& value, const std::string& name)
{
    const auto* table = value.as_table();
    return (table != nullptr) ? table->get(name) : nullptr;
}
}  // namespace

TomlReaderElement::TomlReaderElement() = default;
TomlReaderElement::TomlReaderElement(const std::string& fileName)
{
    TomlReaderElement::loadFile(fileName);
}

bool TomlReaderElement::isValid() const
{
    return ((mCurrent) && (!mCurrent->isNull()));
}
bool TomlReaderElement::isDocument() const
{
    if (mParents.empty()) {
        if (mDoc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<ReaderElement> TomlReaderElement::clone() const
{
    auto ret = std::make_shared<TomlReaderElement>();
    ret->mParents.reserve(mParents.size());
    for (const auto& parent : mParents) {
        ret->mParents.push_back(std::make_shared<TomlElement>(*parent));
    }
    if (mCurrent) {
        ret->mCurrent = std::make_shared<TomlElement>(*mCurrent);
    }
    ret->mDoc = mDoc;
    ret->mIteratorCount = mIteratorCount;
    return ret;
}

bool TomlReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName, std::ios::binary);
    if (file.is_open()) {
        try {
            mDoc = std::make_shared<toml::table>(toml::parse(file, fileName));
            mParents.clear();
            mIteratorCount = 0;
            mCurrent = std::make_shared<TomlElement>(*mDoc, fileName);
            return true;
        }
        catch (const toml::parse_error& error) {
            std::println(stderr, "file read error in {}::{}", fileName, formatError(error));
        }
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

bool TomlReaderElement::parse(const std::string& inputString)
{
    try {
        mDoc = std::make_shared<toml::table>(toml::parse(inputString));
        mParents.clear();
        mIteratorCount = 0;
        mCurrent = std::make_shared<TomlElement>(*mDoc, "string");
        return true;
    }
    catch (const toml::parse_error& error) {
        std::println(stderr, "Read error in stream:: {}", formatError(error));
    }
    mDoc = nullptr;
    mParents.clear();
    if (mCurrent) {
        mCurrent->clear();
    }
    return false;
}

std::string TomlReaderElement::getName() const
{
    return mCurrent ? mCurrent->mName : nullStr;
}
double TomlReaderElement::getValue() const
{
    if (!isValid()) {
        return readerNullVal;
    }

    if (mCurrent->getElement().is_floating_point()) {
        return mCurrent->getElement().as_floating_point()->get();
    }
    if (mCurrent->getElement().is_integer()) {
        return static_cast<double>(mCurrent->getElement().as_integer()->get());
    }
    if (mCurrent->getElement().is_string()) {
        return numeric_conversionComplete(mCurrent->getElement().as_string()->get(), readerNullVal);
    }
    return readerNullVal;
}

std::string TomlReaderElement::getText() const
{
    if (!isValid()) {
        return nullStr;
    }

    if (mCurrent->getElement().is_string()) {
        return mCurrent->getElement().as_string()->get();
    }
    return nullStr;
}

std::string TomlReaderElement::getMultiText(const std::string& /*sep*/) const
{
    if (!isValid()) {
        return nullStr;
    }

    if (mCurrent->getElement().is_string()) {
        return mCurrent->getElement().as_string()->get();
    }
    return nullStr;
}
// Scalar table entries are exposed as attributes.
bool TomlReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }

    if (const auto* value = findEntry(mCurrent->getElement(), attributeName)) {
        return isAttribute(*value);
    }
    return false;
}

bool TomlReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    if (const auto* value = findEntry(mCurrent->getElement(), elementName)) {
        return isElement(*value);
    }

    return false;
}

ReaderAttribute TomlReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!mCurrent->getElement().is_table()) {
        return {};
    }
    const auto tab = orderedEntries(*mCurrent->getElement().as_table());
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    mIteratorCount = 0;

    while (attIterator != elementEnd) {
        if (isAttribute(*attIterator->second)) {
            return {std::string{attIterator->first.str()}, scalarToString(*attIterator->second)};
        }
        ++attIterator;
        ++mIteratorCount;
    }
    return {};
}

ReaderAttribute TomlReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!mCurrent->getElement().is_table()) {
        return {};
    }
    const auto tab = orderedEntries(*mCurrent->getElement().as_table());
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    for (int ii = 0; ii < mIteratorCount && attIterator != elementEnd; ++ii) {
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
        if (isAttribute(*attIterator->second)) {
            return {std::string{attIterator->first.str()}, scalarToString(*attIterator->second)};
        }
        ++attIterator;
        ++mIteratorCount;
    }
    return {};
}

ReaderAttribute TomlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return {attributeName, scalarToString(*findEntry(mCurrent->getElement(), attributeName))};
    }
    return {};
}

std::string TomlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return scalarToString(*findEntry(mCurrent->getElement(), attributeName));
    }
    return nullStr;
}

double TomlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!hasAttribute(attributeName)) {
        return readerNullVal;
    }
    const auto& value = *findEntry(mCurrent->getElement(), attributeName);
    if (value.is_floating_point()) {
        return value.as_floating_point()->get();
    }
    if (value.is_integer()) {
        return static_cast<double>(value.as_integer()->get());
    }
    return numeric_conversionComplete(scalarToString(value), readerNullVal);
}

std::shared_ptr<ReaderElement> TomlReaderElement::firstChild() const
{
    auto newElement = clone();
    newElement->moveToFirstChild();
    return newElement;
}

std::shared_ptr<ReaderElement> TomlReaderElement::firstChild(const std::string& childName) const
{
    auto newElement = clone();
    newElement->moveToFirstChild(childName);
    return newElement;
}

void TomlReaderElement::moveToFirstChild()
{
    if (!isValid()) {
        return;
    }
    mCurrent->mElementIndex = 0;
    if (mCurrent->getElement().is_table()) {
        const auto tab = orderedEntries(*mCurrent->getElement().as_table());
        auto elementIterator = tab.begin();
        auto endIterator = tab.end();

        while (elementIterator != endIterator) {
            if (isElement(*elementIterator->second)) {
                mParents.push_back(mCurrent);
                mCurrent = std::make_shared<TomlElement>(*elementIterator->second,
                                                         std::string{elementIterator->first.str()});
                return;
            }
            ++elementIterator;
            ++mCurrent->mElementIndex;
        }
    }
    mParents.push_back(mCurrent);
    mCurrent = std::make_shared<TomlElement>();
}

void TomlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }
    if (!mCurrent->getElement().is_table()) {
        return;
    }
    const auto entries = orderedEntries(*mCurrent->getElement().as_table());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].first == childName && isElement(*entries[index].second)) {
            mCurrent->mElementIndex = index;
            mParents.push_back(mCurrent);
            mCurrent = std::make_shared<TomlElement>(*entries[index].second, childName);
            return;
        }
    }

    mParents.push_back(mCurrent);
    mCurrent = std::make_shared<TomlElement>();
}

void TomlReaderElement::moveToNextSibling()
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

    const auto tab = orderedEntries(*mParents.back()->getElement().as_table());
    auto elementIterator = tab.begin();
    auto elementEnd = tab.end();
    ++mParents.back()->mElementIndex;
    // Each cloned cursor retains its own table index.
    for (std::size_t ii = 0; ii < mParents.back()->mElementIndex && elementIterator != elementEnd;
         ++ii) {
        ++elementIterator;
        if (elementIterator == elementEnd) {
            mCurrent->clear();
            return;
        }
    }
    // Now find the next valid element
    while (elementIterator != elementEnd) {
        if (isElement(*elementIterator->second)) {
            mCurrent = std::make_shared<TomlElement>(*elementIterator->second,
                                                     std::string{elementIterator->first.str()});
            return;
        }
        ++elementIterator;
        ++mParents.back()->mElementIndex;
    }
    mCurrent->clear();
}

void TomlReaderElement::moveToNextSibling(const std::string& siblingName)
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
        if (!mParents.empty() && mParents.back()->getElement().is_table()) {
            const auto entries = orderedEntries(*mParents.back()->getElement().as_table());
            for (std::size_t index = 0; index < entries.size(); ++index) {
                if (entries[index].first == siblingName && isElement(*entries[index].second)) {
                    mParents.back()->mElementIndex = index;
                    mCurrent = std::make_shared<TomlElement>(*entries[index].second, siblingName);
                    return;
                }
            }
        }
        mCurrent->clear();
    }
}

void TomlReaderElement::moveToParent()
{
    if (mParents.empty()) {
        return;
    }
    mCurrent = mParents.back();
    mParents.pop_back();
}

std::shared_ptr<ReaderElement> TomlReaderElement::nextSibling() const
{
    auto newElement = clone();
    newElement->moveToNextSibling();
    return newElement;
}

std::shared_ptr<ReaderElement> TomlReaderElement::nextSibling(const std::string& siblingName) const
{
    auto newElement = clone();
    newElement->moveToNextSibling(siblingName);
    return newElement;
}

void TomlReaderElement::bookmark()
{
    mBookmarks.push_back(std::static_pointer_cast<TomlReaderElement>(clone()));
}

void TomlReaderElement::restore()
{
    if (mBookmarks.empty()) {
        return;
    }
    mParents = mBookmarks.back()->mParents;
    mCurrent = mBookmarks.back()->mCurrent;
    mDoc = mBookmarks.back()->mDoc;
    mIteratorCount = mBookmarks.back()->mIteratorCount;
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
