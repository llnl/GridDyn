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

namespace {
bool isAttribute(const YAML::Node& testValue)
{
    if (!testValue.IsDefined()) {
        return false;
    }
    return testValue.IsScalar();
}

bool isElement(const YAML::Node& testValue)
{
    if (!testValue.IsDefined()) {
        return false;
    }
    return testValue.IsMap() || testValue.IsSequence();
}
}  // namespace

yamlReaderElement::yamlReaderElement() {}
yamlReaderElement::yamlReaderElement(const std::string& filename)
{
    yamlReaderElement::loadFile(filename);
}
void yamlReaderElement::clear()
{
    if (mCurrent) {
        mCurrent->clear();
    }
    mParents.clear();
}

bool yamlReaderElement::isValid() const
{
    return ((mCurrent) && !(mCurrent->isNull()));
}
bool yamlReaderElement::isDocument() const
{
    if (mParents.empty()) {
        if (mDoc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> yamlReaderElement::clone() const
{
    auto ret = std::make_shared<yamlReaderElement>();
    ret->mParents.reserve(mParents.size());
    for (const auto& parent : mParents) {
        ret->mParents.push_back(std::make_shared<yamlElement>(*parent));
    }
    ret->mCurrent = std::make_shared<yamlElement>(*mCurrent);
    ret->mDoc = mDoc;
    return ret;
}

bool yamlReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        try {
            mDoc = std::make_shared<YAML::Node>(YAML::LoadFile(fileName));
            if ((mDoc->IsSequence()) || (mDoc->IsMap())) {
                mCurrent = std::make_shared<yamlElement>(*mDoc, fileName);
                return true;
            }
            return false;
        }
        catch (const YAML::ParserException& pe) {
            std::println(stderr, "{}", pe.what());
            mDoc = nullptr;
            clear();
            return false;
        }
    }

    std::println(stderr, "unable to open file {}", fileName);
    mDoc = nullptr;
    clear();
    return false;
}

bool yamlReaderElement::parse(const std::string& inputString)
{
    try {
        mDoc = std::make_shared<YAML::Node>(YAML::Load(inputString));

        if (mDoc->IsDefined()) {
            mCurrent = std::make_shared<yamlElement>(*mDoc, "string");
            return true;
        }
        clear();
        mDoc = nullptr;
        return false;
    }
    catch (const YAML::ParserException& pe) {
        std::println(stderr, "{}", pe.what());
        mDoc = nullptr;
        clear();
        return false;
    }
}

std::string yamlReaderElement::getName() const
{
    return mCurrent->mName;
}
double yamlReaderElement::getValue() const
{
    if ((!isValid()) || (!mCurrent->getElement().IsScalar())) {
        return readerNullVal;
    }
    return numeric_conversionComplete<double>(mCurrent->getElement().as<std::string>(),
                                              readerNullVal);
}

std::string yamlReaderElement::getText() const
{
    if ((!isValid()) || (!mCurrent->getElement().IsScalar())) {
        return nullStr;
    }
    return mCurrent->getElement().as<std::string>();
}

std::string yamlReaderElement::getMultiText(const std::string& sep) const
{
    if (!isValid()) {
        return nullStr;
    }
    if (mCurrent->getElement().IsScalar()) {
        return mCurrent->getElement().as<std::string>();
    }
    if (mCurrent->getElement().IsSequence()) {
        std::string ret;
        YAML::const_iterator elementIterator = mCurrent->getElement().begin();
        if (elementIterator->IsScalar()) {
            const YAML::const_iterator elementEnd = mCurrent->getElement().end();
            while (elementIterator != elementEnd) {
                if (!ret.empty()) {
                    ret += sep;
                }
                ret += elementIterator->as<std::string>();
                ++elementIterator;
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
    if (!(mCurrent->getElement().IsMap())) {
        return false;
    }
    if (mCurrent->getElement()[attributeName]) {
        return isAttribute(mCurrent->getElement()[attributeName]);
    }
    return false;
}

bool yamlReaderElement::hasElement(const std::string& elementName) const
{
    if (!isValid()) {
        return false;
    }

    if (mCurrent->getElement()[elementName]) {
        return (isElement(mCurrent->getElement()[elementName]));
    }

    return false;
}

readerAttribute yamlReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return {};
    }

    auto attIterator = mCurrent->getElement().begin();
    auto elementEnd = mCurrent->getElement().end();

    mIteratorCount = 0;
    while (attIterator != elementEnd) {
        if (isAttribute(*attIterator)) {
            return {attIterator->Tag(), attIterator->as<std::string>()};
        }
        ++attIterator;
    }

    return {};
}

readerAttribute yamlReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return {};
    }
    auto attIterator = mCurrent->getElement().begin();
    auto elementEnd = mCurrent->getElement().end();
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
            return {attIterator->Tag(), attIterator->as<std::string>()};
        }
        ++attIterator;
        ++mIteratorCount;
    }
    return {};
}

readerAttribute yamlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return {attributeName, mCurrent->getElement()[attributeName].as<std::string>()};
    }
    return {};
}

std::string yamlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return mCurrent->getElement()[attributeName].as<std::string>();
    }
    return nullStr;
}

double yamlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (hasAttribute(attributeName)) {
        return numeric_conversionComplete(mCurrent->getElement()[attributeName].as<std::string>(),
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
    mCurrent->mElementIndex = 0;
    auto elementIterator = mCurrent->getElement().begin();
    auto endIterator = mCurrent->getElement().end();

    while (elementIterator != endIterator) {
        if (isElement(elementIterator->second)) {
            mParents.push_back(mCurrent);
            mCurrent = std::make_shared<yamlElement>(elementIterator->second,
                                                    elementIterator->first.as<std::string>());
            return;
        }
        ++elementIterator;
        ++mCurrent->mElementIndex;
    }
    mParents.push_back(mCurrent);
    mCurrent->clear();
}

void yamlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }

    if (mCurrent->getElement()[childName]) {
        if (isElement(mCurrent->getElement()[childName])) {
            mParents.push_back(mCurrent);
            mCurrent = std::make_shared<yamlElement>(mCurrent->getElement()[childName], childName);
            return;
        }
    }

    mParents.push_back(mCurrent);
    mCurrent->clear();
}

void yamlReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    ++mCurrent->mArrayIndex;
    while (mCurrent->mArrayIndex < mCurrent->count()) {
        if (!mCurrent->getElement().IsNull()) {
            return;
        }
        ++mCurrent->mArrayIndex;
    }
    if (mParents.empty()) {
        mCurrent = nullptr;
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
        if (isElement(elementIterator->second)) {
            mCurrent = std::make_shared<yamlElement>(elementIterator->second,
                                                    elementIterator->first.as<std::string>());
            return;
        }
        ++elementIterator;
        ++mParents.back()->mElementIndex;
    }
    mCurrent->clear();
}

void yamlReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!isValid()) {
        return;
    }
    if (siblingName == mCurrent->mName) {
        ++mCurrent->mArrayIndex;
        while (mCurrent->mArrayIndex < mCurrent->count()) {
            if (!mCurrent->getElement().IsNull()) {
                return;
            }
            ++mCurrent->mArrayIndex;
        }
        mCurrent->clear();
    } else {
        if (mParents.back()->getElement()[siblingName]) {
            if (isElement(mParents.back()->getElement()[siblingName])) {
                mCurrent = std::make_shared<yamlElement>(mParents.back()->getElement()[siblingName],
                                                        siblingName);
                return;
            }
        }
    }
}

void yamlReaderElement::moveToParent()
{
    if (mParents.empty()) {
        return;
    }
    mCurrent = mParents.back();
    mParents.pop_back();
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
    mBookmarks.push_back(std::static_pointer_cast<yamlReaderElement>(clone()));
}

void yamlReaderElement::restore()
{
    if (mBookmarks.empty()) {
        return;
    }
    mParents = mBookmarks.back()->mParents;
    mCurrent = mBookmarks.back()->mCurrent;
    mBookmarks.pop_back();
}
