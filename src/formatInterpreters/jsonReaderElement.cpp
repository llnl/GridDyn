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
    } catch (const JsonValue::parse_error& err) {
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
    parents.clear();
    if (current) {
        current->clear();
    }
}

bool jsonReaderElement::isValid() const
{
    return ((current) && (!current->isNull()));
}
bool jsonReaderElement::isDocument() const
{
    if (parents.empty()) {
        if (doc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> jsonReaderElement::clone() const
{
    auto ret = std::make_shared<jsonReaderElement>();
    ret->parents.reserve(parents.size());
    for (const auto& parent : parents) {
        ret->parents.push_back(std::make_shared<jsonElement>(*parent));
    }
    ret->current = std::make_shared<jsonElement>(*current);
    ret->doc = doc;
    return ret;
}

bool jsonReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        doc = std::make_shared<JsonValue>();
        std::string errs;
        const bool parseOk = parseJsonStream(file, *doc, errs);
        if (parseOk) {
            current = std::make_shared<jsonElement>(*doc, fileName);
            return true;
        }

        std::println(stderr, "file read error in {}::{}", fileName, errs);
        doc = nullptr;
        clear();
        return false;
    }

    std::println(stderr, "unable to open file {}", fileName);
    doc = nullptr;
    clear();
    return false;
}

bool jsonReaderElement::parse(const std::string& inputString)
{
    std::ifstream file(inputString);
    doc = std::make_shared<JsonValue>();

    if (file.is_open()) {
        std::string errs;
        const bool parseOk = parseJsonStream(file, *doc, errs);
        if (!parseOk) {
            std::println(stderr, "Read error in stream::{}", errs);
            doc = nullptr;
            clear();
            return false;
        }
    } else {
        std::string errs;
        std::istringstream jstring(inputString);
        const bool parseOk = parseJsonStream(jstring, *doc, errs);
        if (!parseOk) {
            std::println(stderr, "Read error in stream::{}", errs);
            doc = nullptr;
            clear();
            return false;
        }
    }
    current = std::make_shared<jsonElement>(*doc, "");
    return true;
}

std::string jsonReaderElement::getName() const
{
    return current->name;
}
double jsonReaderElement::getValue() const
{
    if (!isValid()) {
        return readerNullVal;
    }

    if (current->getElement().is_number()) {
        return current->getElement().get<double>();
    }
    if (current->getElement().is_string()) {
        return numeric_conversionComplete(current->getElement().get<std::string>(), readerNullVal);
    }
    return readerNullVal;
}

std::string jsonReaderElement::getText() const
{
    if (!isValid()) {
        return nullStr;
    }

    if (current->getElement().is_string()) {
        return current->getElement().get<std::string>();
    }
    return nullStr;
}

std::string jsonReaderElement::getMultiText(const std::string& /*sep*/) const
{
    if (!isValid()) {
        return nullStr;
    }

    if (current->getElement().is_string()) {
        return current->getElement().get<std::string>();
    }
    return nullStr;
}
// no attributes in json
bool jsonReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }

    if (current->getElement().is_object() && current->getElement().contains(attributeName)) {
        return (isAttribute(current->getElement()[attributeName]));
    }
    return false;
}

bool jsonReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    if (current->getElement().is_object() && current->getElement().contains(elementName)) {
        return (isElement(current->getElement()[elementName]));
    }

    return false;
}

readerAttribute jsonReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!current->getElement().is_object()) {
        return {};
    }

    auto attIterator = current->getElement().begin();
    auto elementEnd = current->getElement().end();
    iteratorCount = 0;

    while (attIterator != elementEnd) {
        if (isAttribute(*attIterator)) {
            return {attIterator.key(), scalarToString(*attIterator)};
        }
        ++attIterator;
        ++iteratorCount;
    }

    return {};
}

readerAttribute jsonReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return {};
    }
    if (!current->getElement().is_object()) {
        return {};
    }
    auto elementEnd = current->getElement().end();
    auto attIterator = current->getElement().begin();
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
        if (isAttribute(*attIterator)) {
            return {attIterator.key(), scalarToString(*attIterator)};
        }
        ++attIterator;
        ++iteratorCount;
    }
    return {};
}

readerAttribute jsonReaderElement::getAttribute(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return {attributeName, scalarToString(current->getElement()[attributeName])};
    }
    return {};
}

std::string jsonReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return scalarToString(current->getElement()[attributeName]);
    }
    return nullStr;
}

double jsonReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        if (current->getElement()[attributeName].is_number()) {
            return current->getElement()[attributeName].get<double>();
        }
        return numeric_conversionComplete(scalarToString(current->getElement()[attributeName]),
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
    current->elementIndex = 0;
    auto elementIterator = current->getElement().begin();
    auto endIterator = current->getElement().end();

    while (elementIterator != endIterator) {
        if (isElement(*elementIterator)) {
            parents.push_back(current);
            current = std::make_shared<jsonElement>(*elementIterator, elementIterator.key());
            return;
        }
        ++elementIterator;
        ++current->elementIndex;
    }
    parents.push_back(current);
    current->clear();
}

void jsonReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }

    if (current->getElement().contains(childName)) {
        if (isElement(current->getElement()[childName])) {
            parents.push_back(current);
            current = std::make_shared<jsonElement>(current->getElement()[childName], childName);
            return;
        }
    }

    parents.push_back(current);
    current->clear();
}

void jsonReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    ++current->arrayIndex;
    while (current->arrayIndex < current->count()) {
        if (!isJsonNodeEmpty(current->getElement())) {
            return;
        }
        ++current->arrayIndex;
    }
    if (parents.empty()) {
        current->clear();
        return;
    }
    // there are no more elements in a potential array
    auto elementIterator = parents.back()->getElement().begin();
    auto endIterator = parents.back()->getElement().end();
    ++parents.back()->elementIndex;
    // iterators don't survive copy so have to move the iterator to the next element index
    for (int ii = 0; ii < parents.back()->elementIndex; ++ii) {
        ++elementIterator;
        if (elementIterator == endIterator) {
            current->clear();
        }
    }
    // Now find the next valid element
    while (elementIterator != endIterator) {
        if (isElement(*elementIterator)) {
            current = std::make_shared<jsonElement>(*elementIterator, elementIterator.key());
            return;
        }
        ++elementIterator;
        ++parents.back()->elementIndex;
    }
    current->clear();
}

void jsonReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!isValid()) {
        return;
    }
    if (siblingName == current->name) {
        ++current->arrayIndex;
        while (current->arrayIndex < current->count()) {
            if (!isJsonNodeEmpty(current->getElement())) {
                return;
            }
            ++current->arrayIndex;
        }
        current->clear();
    } else {
        if (parents.back()->getElement().contains(siblingName)) {
            if (isElement(parents.back()->getElement()[siblingName])) {
                current = std::make_shared<jsonElement>(parents.back()->getElement()[siblingName],
                                                        siblingName);
                return;
            }
        }
    }
}

void jsonReaderElement::moveToParent()
{
    if (parents.empty()) {
        return;
    }
    current = parents.back();
    parents.pop_back();
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
    bookmarks.push_back(std::static_pointer_cast<jsonReaderElement>(clone()));
}

void jsonReaderElement::restore()
{
    if (bookmarks.empty()) {
        return;
    }
    parents = bookmarks.back()->parents;
    current = bookmarks.back()->current;
    bookmarks.pop_back();
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
