/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "yamlReaderElement.h"

#include "gmlc/utilities/stringConversion.h"
#include "yamlElement.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <print>
#include <string>

static const char nullStr[] = "";
using gmlc::utilities::numeric_conversionComplete;

yamlReaderElement::yamlReaderElement() {}
yamlReaderElement::yamlReaderElement(const std::string& filename)
{
    loadFile(filename);
}
void yamlReaderElement::clear()
{
    if (current) {
        current->clear();
    }
    parents.clear();
}

bool yamlReaderElement::isValid() const
{
    return ((current) && !(current->isNull()));
}
bool yamlReaderElement::isDocument() const
{
    if (parents.empty()) {
        if (doc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> yamlReaderElement::clone() const
{
    auto ret = std::make_shared<yamlReaderElement>();
    ret->parents.reserve(parents.size());
    for (auto& parent : parents) {
        ret->parents.push_back(std::make_shared<yamlElement>(*parent));
    }
    ret->current = std::make_shared<yamlElement>(*current);
    ret->doc = doc;
    return ret;
}

bool yamlReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        try {
            doc = std::make_shared<YAML::Node>(YAML::LoadFile(fileName));
            if ((doc->IsSequence()) || (doc->IsMap())) {
                current = std::make_shared<yamlElement>(*doc, fileName);
                return true;
            }
            return false;
        }
        catch (const YAML::ParserException& pe) {
            std::println(stderr, "{}", pe.what());
            doc = nullptr;
            clear();
            return false;
        }
    }

    std::println(stderr, "unable to open file {}", fileName);
    doc = nullptr;
    clear();
    return false;
}

bool yamlReaderElement::parse(const std::string& inputString)
{
    try {
        doc = std::make_shared<YAML::Node>(YAML::Load(inputString));

        if (doc->IsDefined()) {
            current = std::make_shared<yamlElement>(*doc, "string");
            return true;
        }
        clear();
        doc = nullptr;
        return false;
    }
    catch (const YAML::ParserException& pe) {
        std::println(stderr, "{}", pe.what());
        doc = nullptr;
        clear();
        return false;
    }
}

std::string yamlReaderElement::getName() const
{
    return current->name;
}
double yamlReaderElement::getValue() const
{
    if ((!isValid()) || (!current->getElement().IsScalar())) {
        return readerNullVal;
    }
    return numeric_conversionComplete<double>(current->getElement().as<std::string>(),
                                              readerNullVal);
}

std::string yamlReaderElement::getText() const
{
    if ((!isValid()) || (!current->getElement().IsScalar())) {
        return nullStr;
    }
    return current->getElement().as<std::string>();
}

std::string yamlReaderElement::getMultiText(const std::string& sep) const
{
    if (!isValid()) {
        return nullStr;
    }
    if (current->getElement().IsScalar()) {
        return current->getElement().as<std::string>();
    }
    if (current->getElement().IsSequence()) {
        std::string ret;
        YAML::const_iterator it = current->getElement().begin();
        if (it->IsScalar()) {
            YAML::const_iterator end = current->getElement().end();
            while (it != end) {
                if (!ret.empty()) {
                    ret += sep;
                }
                ret += it->as<std::string>();
                ++it;
            }
        }
        return ret;
    }
    return nullStr;
}

bool yamlReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }
    if (!(current->getElement().IsMap())) {
        return false;
    }
    if (current->getElement()[attributeName]) {
        return (isAttribute(current->getElement()[attributeName]));
    } else {
        return false;
    }
}

bool yamlReaderElement::isAttribute(const YAML::Node& testValue) const
{
    if (!testValue.IsDefined()) {
        return false;
    }
    return testValue.IsScalar();
}

bool yamlReaderElement::isElement(const YAML::Node& testValue) const
{
    if (!testValue.IsDefined()) {
        return false;
    }
    return ((testValue.IsMap()) || (testValue.IsSequence()));
}

bool yamlReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    if (current->getElement()[elementName]) {
        return (isElement(current->getElement()[elementName]));
    }

    return false;
}

readerAttribute yamlReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return readerAttribute();
    }

    auto attIterator = current->getElement().begin();
    auto elementEnd = current->getElement().end();

    iteratorCount = 0;
    while (attIterator != elementEnd) {
        if (isAttribute(*attIterator)) {
            return readerAttribute(attIterator->Tag(), attIterator->as<std::string>());
        }
        ++attIterator;
    }

    return readerAttribute();
}

readerAttribute yamlReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return readerAttribute();
    }
    auto attIterator = current->getElement().begin();
    auto elementEnd = current->getElement().end();
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
        if (isAttribute(*attIterator)) {
            return readerAttribute(attIterator->Tag(), attIterator->as<std::string>());
        }
        ++attIterator;
        ++iteratorCount;
    }
    return readerAttribute();
}

readerAttribute yamlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return readerAttribute(attributeName,
                               current->getElement()[attributeName].as<std::string>());
    }
    return readerAttribute();
}

std::string yamlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return current->getElement()[attributeName].as<std::string>();
    }
    return nullStr;
}

double yamlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return numeric_conversionComplete(current->getElement()[attributeName].as<std::string>(),
                                          readerNullVal);
    }
    return readerNullVal;
}

std::shared_ptr<readerElement> yamlReaderElement::firstChild() const
{
    auto newElement = clone();
    newElement->moveToFirstChild();
    return newElement;
}

std::shared_ptr<readerElement> yamlReaderElement::firstChild(const std::string& childName) const
{
    auto newElement = clone();
    newElement->moveToFirstChild(childName);
    return newElement;
}

void yamlReaderElement::moveToFirstChild()
{
    if (!isValid()) {
        return;
    }
    current->elementIndex = 0;
    auto elementIterator = current->getElement().begin();
    auto endIterator = current->getElement().end();

    while (elementIterator != endIterator) {
        if (isElement(elementIterator->second)) {
            parents.push_back(current);
            current = std::make_shared<yamlElement>(elementIterator->second,
                                                    elementIterator->first.as<std::string>());
            return;
        }
        ++elementIterator;
        ++current->elementIndex;
    }
    parents.push_back(current);
    current->clear();
}

void yamlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }

    if (current->getElement()[childName]) {
        if (isElement(current->getElement()[childName])) {
            parents.push_back(current);
            current = std::make_shared<yamlElement>(current->getElement()[childName], childName);
            return;
        }
    }

    parents.push_back(current);
    current->clear();
}

void yamlReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    ++current->arrayIndex;
    while (current->arrayIndex < current->count()) {
        if (!current->getElement().IsNull()) {
            return;
        }
        ++current->arrayIndex;
    }
    if (parents.empty()) {
        current = nullptr;
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
            return;
        }
    }
    // Now find the next valid element
    while (elementIterator != endIterator) {
        if (isElement(elementIterator->second)) {
            current = std::make_shared<yamlElement>(elementIterator->second,
                                                    elementIterator->first.as<std::string>());
            return;
        }
        ++elementIterator;
        ++parents.back()->elementIndex;
    }
    current->clear();
}

void yamlReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!isValid()) {
        return;
    }
    if (siblingName == current->name) {
        ++current->arrayIndex;
        while (current->arrayIndex < current->count()) {
            if (!current->getElement().IsNull()) {
                return;
            }
            ++current->arrayIndex;
        }
        current->clear();
    } else {
        if (parents.back()->getElement()[siblingName]) {
            if (isElement(parents.back()->getElement()[siblingName])) {
                current = std::make_shared<yamlElement>(parents.back()->getElement()[siblingName],
                                                        siblingName);
                return;
            }
        }
    }
}

void yamlReaderElement::moveToParent()
{
    if (parents.empty()) {
        return;
    }
    current = parents.back();
    parents.pop_back();
}

std::shared_ptr<readerElement> yamlReaderElement::nextSibling() const
{
    auto newElement = clone();
    newElement->moveToNextSibling();
    return newElement;
}

std::shared_ptr<readerElement> yamlReaderElement::nextSibling(const std::string& siblingName) const
{
    auto newElement = clone();
    newElement->moveToNextSibling(siblingName);
    return newElement;
}

void yamlReaderElement::bookmark()
{
    bookmarks.push_back(std::static_pointer_cast<yamlReaderElement>(clone()));
}

void yamlReaderElement::restore()
{
    if (bookmarks.empty()) {
        return;
    }
    parents = bookmarks.back()->parents;
    current = bookmarks.back()->current;
    bookmarks.pop_back();
}
