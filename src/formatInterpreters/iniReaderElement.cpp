/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "iniReaderElement.h"

#include "gmlc/utilities/stringConversion.h"
#include "inih/INIReader.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <print>
#include <string>
#include <utility>

static constexpr char nullStr[] = "";

class gridDynINIReader: public INIReader {
  public:
    using INIReader::INIReader;

    [[nodiscard]] std::pair<std::string, std::string> getAttribute(const std::string& section,
                                                                   int index) const
    {
        if (index < 0) {
            return {};
        }

        const auto prefix = MakeKey(section, "");
        int currentIndex = 0;
        for (const auto& [key, value] : _values) {
            if (key.starts_with(prefix)) {
                if (currentIndex == index) {
                    return {key.substr(prefix.size()), value};
                }
                ++currentIndex;
            }
        }
        return {};
    }
};

iniReaderElement::iniReaderElement() = default;
iniReaderElement::iniReaderElement(const std::string& fileName)
{
    iniReaderElement::loadFile(fileName);
}
void iniReaderElement::clear()
{
    mCurrentSection.clear();
}

static const char invalidString[] = ";";

bool iniReaderElement::isValid() const
{
    return (mCurrentSection != invalidString);
}
bool iniReaderElement::isDocument() const
{
    return ((mDoc) && mCurrentSection.empty());
}

std::shared_ptr<readerElement> iniReaderElement::clone() const
{
    auto ret = std::make_shared<iniReaderElement>();
    ret->mDoc = mDoc;
    ret->mCurrentSection = mCurrentSection;
    ret->mIteratorIndex = mIteratorIndex;
    return ret;
}

bool iniReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        mDoc = std::make_shared<gridDynINIReader>(fileName);
        mCurrentSection = std::string();
        mIteratorIndex = 0;
        return true;
    }

    std::println(stderr, "unable to open file {}", fileName);
    mDoc = nullptr;
    clear();
    return false;
}

bool iniReaderElement::parse(const std::string& /*inputString*/)
{
    return false;
}

std::string iniReaderElement::getName() const
{
    if (mCurrentSection.empty()) {
        return "root";
    }
    return mCurrentSection;
}

double iniReaderElement::getValue() const
{
    return readerNullVal;
}

std::string iniReaderElement::getText() const
{
    return nullStr;
}

std::string iniReaderElement::getMultiText(const std::string& /*sep*/) const
{
    return nullStr;
}

bool iniReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }
    const auto val = mDoc->Get(mCurrentSection, attributeName, "");

    return !(val.empty());
}

bool iniReaderElement::hasElement(const std::string& elementName) const
{
    if (!mCurrentSection.empty()) {
        return false;
    }
    const auto& sec = mDoc->Sections();
    return sec.contains(elementName);
}

readerAttribute iniReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return {};
    }
    const auto& att = mDoc->getAttribute(mCurrentSection, 0);
    mIteratorIndex = 0;
    if ((!att.first.empty()) && (!att.second.empty())) {
        return {att.first, att.second};
    }
    return {};
}

readerAttribute iniReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return {};
    }

    const auto& att = mDoc->getAttribute(mCurrentSection, static_cast<int>(mIteratorIndex + 1));
    ++mIteratorIndex;
    if ((!att.first.empty()) && (!att.second.empty())) {
        return {att.first, att.second};
    }
    return {};
}

readerAttribute iniReaderElement::getAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return {};
    }
    const auto val = mDoc->Get(mCurrentSection, attributeName, "");
    if (!val.empty()) {
        return {attributeName, val};
    }
    return {};
}

std::string iniReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (!isValid()) {
        return nullStr;
    }
    return mDoc->Get(mCurrentSection, attributeName, "");
}

double iniReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!isValid()) {
        return readerNullVal;
    }
    return gmlc::utilities::numeric_conversionComplete(
        mDoc->Get(mCurrentSection, attributeName, ""), readerNullVal);
}

std::shared_ptr<readerElement> iniReaderElement::firstChild() const
{
    auto newElement = clone();
    newElement->moveToFirstChild();
    return newElement;
}

std::shared_ptr<readerElement> iniReaderElement::firstChild(const std::string& childName) const
{
    auto newElement = clone();
    newElement->moveToFirstChild(childName);
    return newElement;
}

void iniReaderElement::moveToFirstChild()
{
    if (!isValid()) {
        return;
    }
    mSectionIndex = 0;
    mIteratorIndex = 0;
    // ini files only have one level of hierarchy
    if (!mCurrentSection.empty()) {
        mCurrentSection = ';';
        return;
    }
    const auto& sec = mDoc->Sections();
    if (sec.empty()) {
        mCurrentSection = ';';
        return;
    }
    mCurrentSection = *sec.begin();
}

void iniReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }
    mSectionIndex = 0;
    mIteratorIndex = 0;
    // ini files only have one level of hierarchy
    if (!mCurrentSection.empty()) {
        mCurrentSection = ';';
        return;
    }
    const auto& sec = mDoc->Sections();
    if (sec.empty()) {
        mCurrentSection = ';';
        return;
    }
    auto sptr = sec.begin();
    while (sptr != sec.end()) {
        if (sptr->starts_with(childName)) {
            mCurrentSection = *sptr;
            return;
        }
        ++mSectionIndex;
        ++sptr;
    }
    mCurrentSection = ';';
}

void iniReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    if (mCurrentSection.empty()) {
        mCurrentSection = ';';
        return;
    }
    ++mSectionIndex;
    mIteratorIndex = 0;
    const auto& secs = mDoc->Sections();
    if (mSectionIndex >= secs.size()) {
        mCurrentSection = ';';
        return;
    }
    auto csec = secs.begin();
    std::advance(csec, static_cast<std::ptrdiff_t>(mSectionIndex));
    mCurrentSection = *csec;
}

void iniReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!isValid()) {
        return;
    }
    moveToNextSibling();
    if (!isValid()) {
        return;
    }
    if (!mCurrentSection.starts_with(siblingName)) {
        mCurrentSection = ';';
        return;
    }
}

void iniReaderElement::moveToParent()
{
    mCurrentSection = "";
    mSectionIndex = 0;
    mIteratorIndex = 0;
}

std::shared_ptr<readerElement> iniReaderElement::nextSibling() const
{
    auto newElement = clone();
    newElement->moveToNextSibling();
    return newElement;
}

std::shared_ptr<readerElement> iniReaderElement::nextSibling(const std::string& siblingName) const
{
    auto newElement = clone();
    newElement->moveToNextSibling(siblingName);
    return newElement;
}

void iniReaderElement::bookmark()
{
    mBookmarks.emplace_back(mCurrentSection, mSectionIndex);
}

void iniReaderElement::restore()
{
    if (mBookmarks.empty()) {
        return;
    }
    mCurrentSection = mBookmarks.back().first;
    mSectionIndex = mBookmarks.back().second;
    mBookmarks.pop_back();
}
