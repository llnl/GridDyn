/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tinyxmlReaderElement.h"

#ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Woverloaded-virtual"
#    include <ticpp/ticpp.h>
#    pragma GCC diagnostic pop
#else
#    include <ticpp/ticpp.h>
#endif

#include "gmlc/utilities/stringConversion.h"
#include <iostream>
#include <memory>
#include <string>

using gmlc::utilities::numeric_conversionComplete;

tinyxmlReaderElement::tinyxmlReaderElement() = default;
tinyxmlReaderElement::tinyxmlReaderElement(const std::string& fileName)
{
    tinyxmlReaderElement::loadFile(fileName);
}
tinyxmlReaderElement::tinyxmlReaderElement(const ticpp::Element* ticppElement,
                                           const ticpp::Element* ticppParent):
    element(ticppElement), parent(ticppParent)
{
}

tinyxmlReaderElement::~tinyxmlReaderElement()
{
    element = nullptr;
    parent = nullptr;
    att = nullptr;
    bookmarks.clear();
    doc = nullptr;
}

void tinyxmlReaderElement::clear()
{
    element = nullptr;
    parent = nullptr;
    att = nullptr;
    bookmarks.clear();
}

bool tinyxmlReaderElement::isValid() const
{
    return ((element != nullptr) || ((parent == nullptr) && (doc)));
}
bool tinyxmlReaderElement::isDocument() const
{
    if (parent == nullptr) {
        if (doc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> tinyxmlReaderElement::clone() const
{
    auto ret = std::make_shared<tinyxmlReaderElement>(element, parent);
    ret->doc = doc;
    return ret;
}

bool tinyxmlReaderElement::loadFile(const std::string& fileName)
{
    try {
        doc = std::make_shared<ticpp::Document>(fileName);
        doc->LoadFile();
        clear();
        element = doc->FirstChildElement(false);
        return true;
    }
    catch (ticpp::Exception& ex) {
        std::cerr << ex.m_details << std::endl;
        doc = nullptr;
        clear();
        return false;
    }
}

bool tinyxmlReaderElement::parse(const std::string& inputString)
{
    doc = std::make_shared<ticpp::Document>();
    try {
        doc->Parse(inputString);
        clear();
        element = doc->FirstChildElement(false);
        return true;
    }
    catch (ticpp::Exception& ex) {
        std::cerr << ex.m_details << std::endl;
        doc = nullptr;
        clear();
        return false;
    }
}

std::string tinyxmlReaderElement::getName() const
{
    std::string name;
    if (element != nullptr) {
        element->GetValue(&name);
    } else if (doc) {
        doc->GetValue(&name);
    }
    return name;
}

double tinyxmlReaderElement::getValue() const
{
    if (element != nullptr) {
        double ret = numeric_conversionComplete(element->GetText(false), readerNullVal);
        return ret;
    }
    return readerNullVal;
}

std::string tinyxmlReaderElement::getText() const
{
    if (element != nullptr) {
        return element->GetText(false);
    }
    return "";
}

std::string tinyxmlReaderElement::getMultiText(const std::string& sep) const
{
    if (element != nullptr) {
        ticpp::Iterator<ticpp::Text> child;
        std::string text;
        std::string ret;
        for (child = child.begin(element); child != child.end(); child++) {
            child->GetValue(&text);
            if (!text.empty()) {
                if (ret.empty()) {
                    ret = text;
                } else {
                    ret += sep + text;
                }
            }
        }
        return ret;
    }
    return "";
}

bool tinyxmlReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (element != nullptr) {
        return element->HasAttribute(attributeName);
    }
    return false;
}

bool tinyxmlReaderElement::hasElement(const std::string& elementName) const
{
    if (element != nullptr) {
        auto testElement = element->FirstChildElement(elementName, false);
        return (testElement != nullptr);
    }
    return false;
}

readerAttribute tinyxmlReaderElement::getFirstAttribute()
{
    if (element != nullptr) {
        att = element->FirstAttribute(false);
        if (att != nullptr) {
            std::string val;
            att->GetValue(&val);
            return readerAttribute(att->Name(), val);
        }
    }
    return readerAttribute();
}

readerAttribute tinyxmlReaderElement::getNextAttribute()
{
    if (att != nullptr) {
        att = att->Next(false);
        if (att != nullptr) {
            std::string val;
            att->GetValue(&val);
            return readerAttribute(att->Name(), val);
        }
    }
    return readerAttribute();
}

readerAttribute tinyxmlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (element != nullptr) {
        std::string val = element->GetAttribute(attributeName);
        if (!val.empty()) {
            return readerAttribute(attributeName, val);
        }
    }
    return readerAttribute();
}

std::string tinyxmlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (element != nullptr) {
        return element->GetAttribute(attributeName);
    }
    return "";
}

double tinyxmlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (element != nullptr) {
        return numeric_conversionComplete(element->GetAttribute(attributeName), readerNullVal);
    }
    return readerNullVal;
}

std::shared_ptr<readerElement> tinyxmlReaderElement::firstChild() const
{
    ticpp::Element* child = nullptr;
    if (element != nullptr) {
        child = element->FirstChildElement(false);
    } else if (isDocument()) {
        child = doc->FirstChildElement(false);
    }
    if (child != nullptr) {
        return std::make_shared<tinyxmlReaderElement>(child, element);
    }
    return nullptr;
}

std::shared_ptr<readerElement> tinyxmlReaderElement::firstChild(const std::string& childName) const
{
    ticpp::Element* child = nullptr;
    if (element != nullptr) {
        child = element->FirstChildElement(childName, false);
    } else if (isDocument()) {
        child = doc->FirstChildElement(childName, false);
    }
    if (child != nullptr) {
        return std::make_shared<tinyxmlReaderElement>(child, element);
    }
    return nullptr;
}

void tinyxmlReaderElement::moveToNextSibling()
{
    if (element != nullptr) {
        element = element->NextSiblingElement(false);
        att = nullptr;
    }
}

void tinyxmlReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (element != nullptr) {
        element = element->NextSiblingElement(siblingName, false);
        att = nullptr;
    }
}

void tinyxmlReaderElement::moveToFirstChild()
{
    if (element != nullptr) {
        parent = element;
        att = nullptr;
        element = element->FirstChildElement(false);
    } else if (isDocument()) {
        element = doc->FirstChildElement(false);
    }
}

void tinyxmlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (element != nullptr) {
        parent = element;
        att = nullptr;
        element = element->FirstChildElement(childName, false);
    } else if (isDocument()) {
        element = doc->FirstChildElement(childName, false);
    }
}

void tinyxmlReaderElement::moveToParent()
{
    if (parent != nullptr) {
        element = parent;
        att = nullptr;
        auto temp = element->Parent(false);
        if (temp != nullptr) {
            if (temp->Type() == TiXmlNode::ELEMENT) {
                parent = temp->ToElement();
            } else {
                parent = nullptr;
            }
        } else {
            parent = nullptr;
        }
    }
}

std::shared_ptr<readerElement> tinyxmlReaderElement::nextSibling() const
{
    if (element != nullptr) {
        auto sibling = element->NextSiblingElement(false);
        if (sibling != nullptr) {
            return std::make_shared<tinyxmlReaderElement>(sibling, parent);
        }
    }
    return nullptr;
}

std::shared_ptr<readerElement>
    tinyxmlReaderElement::nextSibling(const std::string& siblingName) const
{
    if (element != nullptr) {
        auto sibling = element->NextSiblingElement(siblingName, false);
        if (sibling != nullptr) {
            return std::make_shared<tinyxmlReaderElement>(sibling, parent);
        }
    }
    return nullptr;
}

void tinyxmlReaderElement::bookmark()
{
    bookmarks.emplace_back(element, parent);
}
void tinyxmlReaderElement::restore()
{
    if (bookmarks.empty()) {
        return;
    }
    element = bookmarks.back().first;
    parent = bookmarks.back().second;
    bookmarks.pop_back();
}
