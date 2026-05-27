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

class GridDynIniReader: public INIReader {
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

IniReaderElement::IniReaderElement() = default;
IniReaderElement::IniReaderElement(const std::string& fileName)
{
    IniReaderElement::loadFile(fileName);
}
void IniReaderElement::clear()
{
    mCurrentSection.clear();
}

static const char invalidString[] = ";";

bool IniReaderElement::isValid() const
{
    return (mCurrentSection != invalidString);
}
bool IniReaderElement::isDocument() const
{
    return ((mDoc) && mCurrentSection.empty());
}

std::shared_ptr<ReaderElement> IniReaderElement::clone() const
{
    auto ret = std::make_shared<IniReaderElement>();
    ret->mDoc = mDoc;
    ret->mCurrentSection = mCurrentSection;
    ret->mIteratorIndex = mIteratorIndex;
    return ret;
}

bool IniReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        mDoc = std::make_shared<GridDynIniReader>(fileName);
        mCurrentSection = std::string();
        mIteratorIndex = 0;
        return true;
    }

    std::println(stderr, "unable to open file {}", fileName);
    mDoc = nullptr;
    clear();
    return false;
}

bool IniReaderElement::parse(const std::string& /*inputString*/)
{
    return false;
}

std::string IniReaderElement::getName() const
{
    if (mCurrentSection.empty()) {
        return "root";
    }
    return mCurrentSection;
}

double IniReaderElement::getValue() const
{
    return readerNullVal;
}

std::string IniReaderElement::getText() const
{
    return nullStr;
}

std::string IniReaderElement::getMultiText(const std::string& /*sep*/) const
{
    return nullStr;
}

bool IniReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return false;
    }
    const auto val = mDoc->Get(mCurrentSection, attributeName, "");

    return !(val.empty());
}

bool IniReaderElement::hasElement(const std::string& elementName) const
{
    if (!mCurrentSection.empty()) {
        return false;
    }
    const auto& sec = mDoc->Sections();
    return sec.contains(elementName);
}

ReaderAttribute IniReaderElement::getFirstAttribute()
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

ReaderAttribute IniReaderElement::getNextAttribute()
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

ReaderAttribute IniReaderElement::getAttribute(const std::string& attributeName) const
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

std::string IniReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (!isValid()) {
        return nullStr;
    }
    return mDoc->Get(mCurrentSection, attributeName, "");
}

double IniReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!isValid()) {
        return readerNullVal;
    }
    return gmlc::utilities::numeric_conversionComplete(
        mDoc->Get(mCurrentSection, attributeName, ""), readerNullVal);
}

std::shared_ptr<ReaderElement> IniReaderElement::firstChild() const
{
    auto newElement = clone();
    newElement->moveToFirstChild();
    return newElement;
}

std::shared_ptr<ReaderElement> IniReaderElement::firstChild(const std::string& childName) const
{
    auto newElement = clone();
    newElement->moveToFirstChild(childName);
    return newElement;
}

void IniReaderElement::moveToFirstChild()
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

void IniReaderElement::moveToFirstChild(const std::string& childName)
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

void IniReaderElement::moveToNextSibling()
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

void IniReaderElement::moveToNextSibling(const std::string& siblingName)
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

void IniReaderElement::moveToParent()
{
    mCurrentSection = "";
    mSectionIndex = 0;
    mIteratorIndex = 0;
}

std::shared_ptr<ReaderElement> IniReaderElement::nextSibling() const
{
    auto newElement = clone();
    newElement->moveToNextSibling();
    return newElement;
}

std::shared_ptr<ReaderElement> IniReaderElement::nextSibling(const std::string& siblingName) const
{
    auto newElement = clone();
    newElement->moveToNextSibling(siblingName);
    return newElement;
}

void IniReaderElement::bookmark()
{
    mBookmarks.emplace_back(mCurrentSection, mSectionIndex);
}

void IniReaderElement::restore()
{
    if (mBookmarks.empty()) {
        return;
    }
    mCurrentSection = mBookmarks.back().first;
    mSectionIndex = mBookmarks.back().second;
    mBookmarks.pop_back();
}
