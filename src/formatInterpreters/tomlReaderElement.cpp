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
tomlReaderElement::tomlReaderElement(const std::string& fileName):
    doc(nullptr), parents(), current(nullptr), iteratorCount(0), bookmarks()
{
    tomlReaderElement::loadFile(fileName);
}

bool tomlReaderElement::isValid() const
{
    return ((current) && (!current->isNull()));
}
bool tomlReaderElement::isDocument() const
{
    if (parents.empty()) {
        if (doc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> tomlReaderElement::clone() const
{
    auto ret = std::make_shared<tomlReaderElement>();
    ret->parents.reserve(parents.size());
    for (const auto& parent : parents) {
        ret->parents.push_back(std::make_shared<tomlElement>(*parent));
    }
    ret->current = std::make_shared<tomlElement>(*current);
    ret->doc = doc;
    return ret;
}

bool tomlReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName, std::ios::binary);
    if (file.is_open()) {
        auto parseResult = toml::try_parse<toml::ordered_type_config>(file, fileName);
        if (parseResult.is_ok()) {
            doc = std::make_shared<TomlValue>(parseResult.unwrap());
            current = std::make_shared<tomlElement>(*doc, fileName);
            return true;
        }

        std::println(stderr,
                     "file read error in {}::{}",
                     fileName,
                     formatErrors(parseResult.unwrap_err()));
        doc = nullptr;
        parents.clear();
        if (current) {
            current->clear();
        }
        return false;
    }

    std::println(stderr, "unable to open file {}", fileName);
    doc = nullptr;
    parents.clear();
    if (current) {
        current->clear();
    }
    return false;
}

bool tomlReaderElement::parse(const std::string& inputString)
{
    auto parseResult = toml::try_parse_str<toml::ordered_type_config>(inputString);
    if (parseResult.is_ok()) {
        doc = std::make_shared<TomlValue>(parseResult.unwrap());
        current = std::make_shared<tomlElement>(*doc, "string");
        return true;
    }

    std::println(stderr, "Read error in stream:: {}", formatErrors(parseResult.unwrap_err()));
    doc = nullptr;
    parents.clear();
    if (current) {
        current->clear();
    }
    return false;
}

std::string tomlReaderElement::getName() const
{
    return current->name;
}
double tomlReaderElement::getValue() const
{
    if (!isValid()) {
        return readerNullVal;
    }

    if (current->getElement().is_floating()) {
        return current->getElement().as_floating();
    }
    if (current->getElement().is_integer()) {
        return static_cast<double>(current->getElement().as_integer());
    }
    if (current->getElement().is_string()) {
        return numeric_conversionComplete(current->getElement().as_string(), readerNullVal);
    }
    return readerNullVal;
}

std::string tomlReaderElement::getText() const
{
    if (!isValid()) {
        return nullStr;
    }

    if (current->getElement().is_string()) {
        return current->getElement().as_string();
    }
    return nullStr;
}

std::string tomlReaderElement::getMultiText(const std::string& /*sep*/) const
{
    if (!isValid()) {
        return nullStr;
    }

    if (current->getElement().is_string()) {
        return current->getElement().as_string();
    }
    return nullStr;
}
// no attributes in toml
bool tomlReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }

    if (current->getElement().is_table() && current->getElement().contains(attributeName)) {
        return isAttribute(current->getElement().at(attributeName));
    }
    return false;
}

bool tomlReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    if (current->getElement().is_table() && current->getElement().contains(elementName)) {
        return isElement(current->getElement().at(elementName));
    }

    return false;
}

readerAttribute tomlReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!current->getElement().is_table()) {
        return {};
    }
    const auto& tab = current->getElement().as_table();
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    iteratorCount = 0;

    while (attIterator != elementEnd) {
        if (isAttribute(attIterator->second)) {
            return {attIterator->first, scalarToString(attIterator->second)};
        }
        ++attIterator;
        ++iteratorCount;
    }
    return {};
}

readerAttribute tomlReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!current->getElement().is_table()) {
        return {};
    }
    const auto& tab = current->getElement().as_table();
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    for (int ii = 0; ii < iteratorCount; ++ii) {
        ++attIterator;
        if (attIterator == elementEnd) {
            return {};
        }
    }
    if (attIterator == elementEnd) {
        return {};
    }
    ++attIterator;
    ++iteratorCount;
    while (attIterator != elementEnd) {
        if (isAttribute(attIterator->second)) {
            return {attIterator->first, scalarToString(attIterator->second)};
        }
        ++attIterator;
        ++iteratorCount;
    }
    return {};
}

readerAttribute tomlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return {attributeName, scalarToString(current->getElement().at(attributeName))};
    }
    return {};
}

std::string tomlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return scalarToString(current->getElement().at(attributeName));
    }
    return nullStr;
}

double tomlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!hasAttribute(attributeName)) {
        return readerNullVal;
    }
    const auto& value = current->getElement().at(attributeName);
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
    current->elementIndex = 0;
    if (current->getElement().is_table()) {
        const auto& tab = current->getElement().as_table();
        auto elementIterator = tab.begin();
        auto endIterator = tab.end();

        while (elementIterator != endIterator) {
            if (isElement(elementIterator->second)) {
                parents.push_back(current);
                current =
                    std::make_shared<tomlElement>(elementIterator->second, elementIterator->first);
                return;
            }
            ++elementIterator;
            ++current->elementIndex;
        }
    }
    parents.push_back(current);
    current->clear();
}

void tomlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }
    if (!current->getElement().is_table()) {
        return;
    }
    if (current->getElement().contains(childName) &&
        isElement(current->getElement().at(childName))) {
        parents.push_back(current);
        current = std::make_shared<tomlElement>(current->getElement().at(childName), childName);
        return;
    }

    parents.push_back(current);
    current->clear();
}

void tomlReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    ++current->arrayIndex;
    while (current->arrayIndex < current->count()) {
        if (!isTomlNodeEmpty(current->getElement())) {
            return;
        }
        ++current->arrayIndex;
    }
    if (parents.empty()) {
        current->clear();
        return;
    }
    // there are no more elements in a potential array

    const auto& tab = parents.back()->getElement().as_table();
    auto elementIterator = tab.begin();
    auto elementEnd = tab.end();
    ++parents.back()->elementIndex;
    // iterators don't survive copy so have to move the iterator to the next element index
    for (int ii = 0; ii < parents.back()->elementIndex; ++ii) {
        ++elementIterator;
        if (elementIterator == elementEnd) {
            current->clear();
        }
    }
    // Now find the next valid element
    while (elementIterator != elementEnd) {
        if (isElement(elementIterator->second)) {
            current =
                std::make_shared<tomlElement>(elementIterator->second, elementIterator->first);
            return;
        }
        ++elementIterator;
        ++parents.back()->elementIndex;
    }
    current->clear();
}

void tomlReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!isValid()) {
        return;
    }
    if (siblingName == current->name) {
        ++current->arrayIndex;
        while (current->arrayIndex < current->count()) {
            if (!isTomlNodeEmpty(current->getElement())) {
                return;
            }
            ++current->arrayIndex;
        }
        current->clear();
    } else {
        if (!parents.empty() && parents.back()->getElement().is_table() &&
            parents.back()->getElement().contains(siblingName) &&
            isElement(parents.back()->getElement().at(siblingName))) {
            current = std::make_shared<tomlElement>(parents.back()->getElement().at(siblingName),
                                                    siblingName);
            return;
        }
    }
}

void tomlReaderElement::moveToParent()
{
    if (parents.empty()) {
        return;
    }
    current = parents.back();
    parents.pop_back();
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
    bookmarks.push_back(std::static_pointer_cast<tomlReaderElement>(clone()));
}

void tomlReaderElement::restore()
{
    if (bookmarks.empty()) {
        return;
    }
    parents = bookmarks.back()->parents;
    current = bookmarks.back()->current;
    bookmarks.pop_back();
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
