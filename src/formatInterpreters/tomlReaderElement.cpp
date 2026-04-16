/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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

static const char nullStr[] = "";
using gmlc::utilities::numeric_conversionComplete;

bool isElement(const toml::Value& testValue);
bool isAttribute(const toml::Value& testValue);
tomlReaderElement::tomlReaderElement() = default;
tomlReaderElement::tomlReaderElement(const std::string& fileName)
{
    tomlReaderElement::loadFile(fileName);
}
void tomlReaderElement::clear()
{
    parents.clear();
    if (current) {
        current->clear();
    }
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
    for (auto& parent : parents) {
        ret->parents.push_back(std::make_shared<tomlElement>(*parent));
    }
    ret->current = std::make_shared<tomlElement>(*current);
    ret->doc = doc;
    return ret;
}

bool tomlReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        doc = std::make_shared<toml::ParseResult>(toml::parse(file));
        if (doc->valid()) {
            current = std::make_shared<tomlElement>(doc->value, fileName);
            return true;
        }

        std::println(stderr, "file read error in {}::{}", fileName, doc->errorReason);
        doc = nullptr;
        clear();
        return false;
    }

    std::println(stderr, "unable to open file {}", fileName);
    doc = nullptr;
    clear();
    return false;
}

bool tomlReaderElement::parse(const std::string& inputString)
{
    std::istringstream sstream(inputString);
    doc = std::make_shared<toml::ParseResult>(toml::parse(sstream));
    if (doc->valid()) {
        current = std::make_shared<tomlElement>(doc->value, "string");
        return true;
    }

    std::println(stderr, "Read error in stream:: {}", doc->errorReason);
    doc = nullptr;
    clear();
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

    if (current->getElement().is<double>()) {
        return current->getElement().as<double>();
    }
    if (current->getElement().is<std::string>()) {
        return numeric_conversionComplete(current->getElement().as<std::string>(), readerNullVal);
    }
    return readerNullVal;
}

std::string tomlReaderElement::getText() const
{
    if (!isValid()) {
        return nullStr;
    }

    if (current->getElement().is<std::string>()) {
        return current->getElement().as<std::string>();
    }
    return nullStr;
}

std::string tomlReaderElement::getMultiText(const std::string& /*sep*/) const
{
    if (!isValid()) {
        return nullStr;
    }

    if (current->getElement().is<std::string>()) {
        return current->getElement().as<std::string>();
    }
    return nullStr;
}
// no attributes in toml
bool tomlReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }

    auto att = current->getElement().find(attributeName);
    if (att != nullptr) {
        return (isAttribute(*att));
    }
    return false;
}

bool tomlReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    auto att = current->getElement().find(elementName);
    if (att != nullptr) {
        return (isElement(*att));
    }

    return false;
}

readerAttribute tomlReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return readerAttribute();
    }
    if (current->getElement().type() != toml::Value::TABLE_TYPE) {
        return readerAttribute();
    }
    auto& tab = current->getElement().as<toml::Table>();
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    iteratorCount = 0;

    while (attIterator != elementEnd) {
        if (isAttribute(attIterator->second)) {
            return readerAttribute(attIterator->first, attIterator->second.as<std::string>());
        }
        ++attIterator;
        ++iteratorCount;
    }
    return readerAttribute();
}

readerAttribute tomlReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return readerAttribute();
    }
    auto& tab = current->getElement().as<toml::Table>();
    auto attIterator = tab.begin();
    auto elementEnd = tab.end();
    for (int ii = 0; ii < iteratorCount; ++ii) {
        ++attIterator;
        if (attIterator == elementEnd) {
            return readerAttribute();
        }
    }
    if (attIterator == elementEnd) {
        return readerAttribute();
    }
    ++attIterator;
    ++iteratorCount;
    while (attIterator != elementEnd) {
        if (isAttribute(attIterator->second)) {
            return readerAttribute(attIterator->first, attIterator->second.as<std::string>());
        }
        ++attIterator;
        ++iteratorCount;
    }
    return readerAttribute();
}

readerAttribute tomlReaderElement::getAttribute(const std::string& attributeName) const
{
    auto v = current->getElement().find(attributeName);
    if ((v != nullptr) && (isAttribute(*v))) {
        return readerAttribute(attributeName, v->as<std::string>());
    }
    return readerAttribute();
}

std::string tomlReaderElement::getAttributeText(const std::string& attributeName) const
{
    auto v = current->getElement().find(attributeName);
    if ((v != nullptr) && (isAttribute(*v))) {
        return v->as<std::string>();
    }
    return nullStr;
}

double tomlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    auto v = current->getElement().find(attributeName);
    if (v == nullptr) {
        return readerNullVal;
    }
    if (v->isNumber()) {
        return v->asNumber();
    }
    return numeric_conversionComplete(v->as<std::string>(), readerNullVal);
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
    if (current->getElement().type() == toml::Value::TABLE_TYPE) {
        auto& tab = current->getElement().as<toml::Table>();
        auto elementIterator = tab.begin();
        auto endIterator = tab.end();

        while (elementIterator != endIterator) {
            if (isElement(elementIterator->second)) {
                parents.push_back(current);
                current =
                    std::make_shared<tomlElement>(elementIterator->first, elementIterator->first);
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
    if (current->getElement().type() != toml::Value::TABLE_TYPE) {
        return;
    }
    auto v = current->getElement().findChild(childName);
    if ((v != nullptr) && (isElement(*v))) {
        parents.push_back(current);
        current = std::make_shared<tomlElement>(*v, childName);
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
        if (!current->getElement().empty()) {
            return;
        }
        ++current->arrayIndex;
    }
    if (parents.empty()) {
        current->clear();
        return;
    }
    // there are no more elements in a potential array

    auto& tab = parents.back()->getElement().as<toml::Table>();
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
        if (isElement(elementIterator->first)) {
            current = std::make_shared<tomlElement>(elementIterator->first, elementIterator->first);
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
            if (!current->getElement().empty()) {
                return;
            }
            ++current->arrayIndex;
        }
        current->clear();
    } else {
        auto v = parents.back()->getElement().find(siblingName);
        if ((v != nullptr) && (isElement(*v))) {
            current = std::make_shared<tomlElement>(*v, siblingName);
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

bool isAttribute(const toml::Value& testValue)
{
    if (testValue.empty()) {
        return false;
    }
    if (testValue.type() == toml::Value::ARRAY_TYPE) {
        return false;
    }
    if (testValue.type() == toml::Value::TABLE_TYPE) {
        return false;
    }
    return true;
}

bool isElement(const toml::Value& testValue)
{
    if (testValue.empty()) {
        return false;
    }

    return (!isAttribute(testValue));
}
