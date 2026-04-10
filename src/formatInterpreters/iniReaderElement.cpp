/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "iniReaderElement.h"

#include "gmlc/utilities/stringConversion.h"
#include "inih/INIReader.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

static const char nullStr[] = "";

iniReaderElement::iniReaderElement() = default;
iniReaderElement::iniReaderElement(const std::string& fileName)
{
    iniReaderElement::loadFile(fileName);
}
void iniReaderElement::clear()
{
    currentSection.clear();
}

static const char invalidString[] = ";";

bool iniReaderElement::isValid() const
{
    return (currentSection != invalidString);
}
bool iniReaderElement::isDocument() const
{
    return ((doc) && currentSection.empty());
}

std::shared_ptr<readerElement> iniReaderElement::clone() const
{
    auto ret = std::make_shared<iniReaderElement>();
    ret->doc = doc;
    ret->currentSection = currentSection;
    ret->iteratorIndex = iteratorIndex;
    return ret;
}

bool iniReaderElement::loadFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (file.is_open()) {
        doc = std::make_shared<INIReader>(fileName);
        currentSection = std::string();
        iteratorIndex = 0;
        return true;
    }

    std::cerr << "unable to open file " << fileName << '\n';
    doc = nullptr;
    clear();
    return false;
}

bool iniReaderElement::parse(const std::string& /*inputString*/)
{
    return false;
}

std::string iniReaderElement::getName() const
{
    if (currentSection.empty()) {
        return "root";
    }
    return currentSection;
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
    auto& val = doc->Get(currentSection, attributeName);

    return !(val.empty());
}

bool iniReaderElement::hasElement(const std::string& elementName) const
{
    if (!currentSection.empty()) {
        return false;
    }
    auto& sec = doc->Sections();
    return (sec.find(elementName) != sec.end());
}

readerAttribute iniReaderElement::getFirstAttribute()
{
    if (!isValid()) {
        return readerAttribute();
    }
    auto& att = doc->getAttribute(currentSection, 0);
    iteratorIndex = 0;
    if ((!att.first.empty()) && (!att.second.empty())) {
        return readerAttribute(att.first, att.second);
    }
    return readerAttribute();
}

readerAttribute iniReaderElement::getNextAttribute()
{
    if (!isValid()) {
        return readerAttribute();
    }

    auto& att = doc->getAttribute(currentSection, iteratorIndex + 1);
    ++iteratorIndex;
    if ((!att.first.empty()) && (!att.second.empty())) {
        return readerAttribute(att.first, att.second);
    }
    return readerAttribute();
}

readerAttribute iniReaderElement::getAttribute(const std::string& attributeName) const
{
    if (!isValid()) {
        return readerAttribute();
    }
    auto& val = doc->Get(currentSection, attributeName);
    if (!val.empty()) {
        return readerAttribute(attributeName, val);
    }
    return readerAttribute();
}

std::string iniReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (!isValid()) {
        return nullStr;
    }
    return doc->Get(currentSection, attributeName);
}

double iniReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!isValid()) {
        return readerNullVal;
    }
    return gmlc::utilities::numeric_conversionComplete(doc->Get(currentSection, attributeName),
                                                       readerNullVal);
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
    sectionIndex = 0;
    iteratorIndex = 0;
    // ini files only have one level of hierarchy
    if (!currentSection.empty()) {
        currentSection = ';';
        return;
    }
    auto& sec = doc->Sections();
    if (sec.empty()) {
        currentSection = ';';
        return;
    }
    currentSection = *sec.begin();
}

void iniReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!isValid()) {
        return;
    }
    sectionIndex = 0;
    iteratorIndex = 0;
    // ini files only have one level of hierarchy
    if (!currentSection.empty()) {
        currentSection = ';';
        return;
    }
    auto& sec = doc->Sections();
    if (sec.empty()) {
        currentSection = ';';
        return;
    }
    auto sptr = sec.begin();
    while (sptr != sec.end()) {
        if (sptr->find(childName) == 0) {
            currentSection = *sptr;
        }
        ++sectionIndex;
    }
}

void iniReaderElement::moveToNextSibling()
{
    if (!isValid()) {
        return;
    }
    if (currentSection.empty()) {
        currentSection = ';';
        return;
    }
    ++sectionIndex;
    iteratorIndex = 0;
    auto& secs = doc->Sections();
    if (sectionIndex >= static_cast<int>(secs.size())) {
        currentSection = ';';
        return;
    }
    int ccnt = 0;

    auto csec = secs.begin();
    while (ccnt < sectionIndex) {
        ++ccnt;
        ++csec;
    }
    currentSection = *csec;
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
    if (currentSection.find(siblingName) != 0) {
        currentSection = ';';
        return;
    }
}

void iniReaderElement::moveToParent()
{
    currentSection = "";
    sectionIndex = 0;
    iteratorIndex = 0;
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
    bookmarks.emplace_back(currentSection, sectionIndex);
}

void iniReaderElement::restore()
{
    if (bookmarks.empty()) {
        return;
    }
    currentSection = bookmarks.back().first;
    sectionIndex = bookmarks.back().second;
    bookmarks.pop_back();
}
