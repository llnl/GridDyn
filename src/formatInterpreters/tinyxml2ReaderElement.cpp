/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tinyxml2ReaderElement.h"

#include "gmlc/utilities/string_viewConversion.h"
#include <tinyxml2/tinyxml2.h>

using namespace tinyxml2;
using namespace gmlc::utilities;

// this is not using default for gcc 4.9 compatibility
tinyxml2ReaderElement::tinyxml2ReaderElement() = default;
tinyxml2ReaderElement::tinyxml2ReaderElement(const std::string& fileName)
{
    tinyxml2ReaderElement::loadFile(fileName);
}
tinyxml2ReaderElement::tinyxml2ReaderElement(const XMLElement* xmlElement,
                                             const XMLElement* xmlParent):
    element(xmlElement), parent(xmlParent)
{
}

tinyxml2ReaderElement::~tinyxml2ReaderElement() = default;
void tinyxml2ReaderElement::clear()
{
    element = nullptr;
    parent = nullptr;
    att = nullptr;
    bookmarks.clear();
}

bool tinyxml2ReaderElement::isValid() const
{
    return ((element != nullptr) || ((parent == nullptr) && (doc)));
}
bool tinyxml2ReaderElement::isDocument() const
{
    if (parent == nullptr) {
        if (doc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> tinyxml2ReaderElement::clone() const
{
    auto ret = std::make_shared<tinyxml2ReaderElement>(element, parent);
    ret->doc = doc;
    return ret;
}

bool tinyxml2ReaderElement::loadFile(const std::string& fileName)
{
    doc = std::make_shared<XMLDocument>(true, COLLAPSE_WHITESPACE);
    XMLError res = doc->LoadFile(fileName.c_str());
    clear();
    if (res == XML_SUCCESS) {
        element = doc->FirstChildElement();
        return true;
    }
    doc->PrintError();
    doc = nullptr;
    return false;
}

bool tinyxml2ReaderElement::parse(const std::string& inputString)
{
    doc = std::make_shared<XMLDocument>(true, COLLAPSE_WHITESPACE);
    XMLError res = doc->Parse(inputString.data(), inputString.length());
    clear();
    if (res == XML_SUCCESS) {
        element = doc->FirstChildElement();
        return true;
    }

    doc = nullptr;
    return false;
}

std::string tinyxml2ReaderElement::getName() const
{
    if (element != nullptr) {
        auto cname = element->Name();
        if (cname != nullptr) {
            return std::string(cname);
        }
    }
    return "";
}

double tinyxml2ReaderElement::getValue() const
{
    if (element != nullptr) {
        auto cText = element->GetText();
        if (cText != nullptr) {
            double val = numeric_conversionComplete(cText, readerNullVal);
            return val;
        }
        // double ret = numeric_conversionComplete(element->GetText(false), kNullVal);
        // return ret;
    }
    return readerNullVal;
}

std::string tinyxml2ReaderElement::getText() const
{
    if (element != nullptr) {
        auto cname = element->GetText();
        if (cname != nullptr) {
            return std::string(cname);
        }
    }
    return "";
}

std::string tinyxml2ReaderElement::getMultiText(const std::string& sep) const
{
    std::string ret;
    if (element != nullptr) {
        auto childNode = element->FirstChild();
        while (childNode != nullptr) {
            auto textChildNode = childNode->ToText();
            if (textChildNode != nullptr) {
                auto c = textChildNode->Value();
                if (ret.empty()) {
                    ret = std::string(c);
                } else {
                    ret += sep + std::string(c);
                }
            }
            childNode = childNode->NextSibling();
        }
    }
    return ret;
}

bool tinyxml2ReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (element != nullptr) {
        auto Att = element->Attribute(attributeName.c_str());
        return (Att != nullptr);
    }
    return false;
}

bool tinyxml2ReaderElement::hasElement(const std::string& elementName) const
{
    if (element != nullptr) {
        auto testElement = element->FirstChildElement(elementName.c_str());
        return (testElement != nullptr);
    }
    return false;
}

readerAttribute tinyxml2ReaderElement::getFirstAttribute()
{
    if (element != nullptr) {
        att = element->FirstAttribute();
        if (att != nullptr) {
            return readerAttribute(std::string(att->Name()), std::string(att->Value()));
        }
    }
    return readerAttribute();
}

readerAttribute tinyxml2ReaderElement::getNextAttribute()
{
    if (att != nullptr) {
        att = att->Next();
        if (att != nullptr) {
            return readerAttribute(std::string(att->Name()), std::string(att->Value()));
        }
    }
    return readerAttribute();
}

readerAttribute tinyxml2ReaderElement::getAttribute(const std::string& attributeName) const
{
    if (element != nullptr) {
        auto c = element->Attribute(attributeName.c_str());
        if (c != nullptr) {
            return readerAttribute(attributeName, std::string(c));
        }
    }
    return readerAttribute();
}

std::string tinyxml2ReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (element != nullptr) {
        auto c = element->Attribute(attributeName.c_str());
        if (c != nullptr) {
            return std::string(c);
        }
    }
    return "";
}

double tinyxml2ReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (element != nullptr) {
        auto c = element->Attribute(attributeName.c_str());
        if (c != nullptr) {
            double val = numeric_conversionComplete(c, readerNullVal);
            return val;
        }
    }
    return readerNullVal;
}

std::shared_ptr<readerElement> tinyxml2ReaderElement::firstChild() const
{
    const XMLElement* child = nullptr;
    if (element != nullptr) {
        child = element->FirstChildElement();
    } else if (isDocument()) {
        child = doc->FirstChildElement();
    }
    if (child != nullptr) {
        auto child1 = std::make_shared<tinyxml2ReaderElement>(child, element);
        return child1;
    }
    return nullptr;
}

std::shared_ptr<readerElement> tinyxml2ReaderElement::firstChild(const std::string& childName) const
{
    const XMLElement* child = nullptr;
    if (element != nullptr) {
        child = element->FirstChildElement(childName.c_str());
    } else if (isDocument()) {
        child = doc->FirstChildElement(childName.c_str());
    }
    if (child != nullptr) {
        auto child1 = std::make_shared<tinyxml2ReaderElement>(child, element);
        return child1;
    }
    return nullptr;
}

void tinyxml2ReaderElement::moveToNextSibling()
{
    if (element != nullptr) {
        element = element->NextSiblingElement();
        att = nullptr;
    }
}

void tinyxml2ReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (element != nullptr) {
        element = element->NextSiblingElement(siblingName.c_str());
        att = nullptr;
    }
}

void tinyxml2ReaderElement::moveToFirstChild()
{
    if (element != nullptr) {
        parent = element;
        att = nullptr;
        element = element->FirstChildElement();
    } else if (isDocument()) {
        element = doc->FirstChildElement();
    }
}

void tinyxml2ReaderElement::moveToFirstChild(const std::string& childName)
{
    if (element != nullptr) {
        parent = element;
        att = nullptr;
        element = element->FirstChildElement(childName.c_str());
    } else if (isDocument()) {
        element = doc->FirstChildElement(childName.c_str());
    }
}

void tinyxml2ReaderElement::moveToParent()
{
    if (parent != nullptr) {
        element = parent;
        att = nullptr;
        auto parentNode = element->Parent();
        if (parentNode != nullptr) {
            parent = parentNode->ToElement();
        } else {
            parent = nullptr;
        }
    }
}

std::shared_ptr<readerElement> tinyxml2ReaderElement::nextSibling() const
{
    if (element != nullptr) {
        auto sibling = element->NextSiblingElement();
        if (sibling != nullptr) {
            return std::make_shared<tinyxml2ReaderElement>(sibling, parent);
        }
    }
    return nullptr;
}

std::shared_ptr<readerElement>
    tinyxml2ReaderElement::nextSibling(const std::string& siblingName) const
{
    if (element != nullptr) {
        auto sibling = element->NextSiblingElement(siblingName.c_str());
        if (sibling != nullptr) {
            return std::make_shared<tinyxml2ReaderElement>(sibling, parent);
        }
    }
    return nullptr;
}

void tinyxml2ReaderElement::bookmark()
{
    bookmarks.emplace_back(element, parent);
}
void tinyxml2ReaderElement::restore()
{
    if (bookmarks.empty()) {
        return;
    }
    element = bookmarks.back().first;
    parent = bookmarks.back().second;
    bookmarks.pop_back();
}
