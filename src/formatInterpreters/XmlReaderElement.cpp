/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "XmlReaderElement.h"

#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include <iostream>
#include <memory>
#include <string>

using gmlc::utilities::numeric_conversionComplete;

XmlReaderElement::XmlReaderElement() = default;
XmlReaderElement::XmlReaderElement(const std::string& fileName)
{
    XmlReaderElement::loadFile(fileName);
}
XmlReaderElement::XmlReaderElement(pugi::xml_node xmlElement, pugi::xml_node xmlParent):
    element(xmlElement), parent(xmlParent)
{
}

XmlReaderElement::~XmlReaderElement() = default;
void XmlReaderElement::clear()
{
    element = pugi::xml_node();
    parent = pugi::xml_node();
    att = pugi::xml_attribute();
    bookmarks.clear();
}

bool XmlReaderElement::isValid() const
{
    return (static_cast<bool>(element) || (!parent && doc));
}
bool XmlReaderElement::isDocument() const
{
    if (!parent) {
        if (doc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> XmlReaderElement::clone() const
{
    auto ret = std::make_shared<XmlReaderElement>(element, parent);
    ret->doc = doc;
    return ret;
}

bool XmlReaderElement::loadFile(const std::string& fileName)
{
    doc = std::make_shared<pugi::xml_document>();
    const auto res = doc->load_file(fileName.c_str());
    clear();
    if (res) {
        element = doc->document_element();
        return true;
    }
    std::cerr << "unable to load xml file " << fileName << ": " << res.description() << '\n';
    doc = nullptr;
    return false;
}

bool XmlReaderElement::parse(const std::string& inputString)
{
    doc = std::make_shared<pugi::xml_document>();
    const auto res = doc->load_buffer(inputString.data(), inputString.length());
    clear();
    if (res) {
        element = doc->document_element();
        return true;
    }
    std::cerr << "unable to parse xml string: " << res.description() << '\n';
    doc = nullptr;
    return false;
}

std::string XmlReaderElement::getName() const
{
    if (!element.empty()) {
        return element.name();
    }
    return "";
}

double XmlReaderElement::getValue() const
{
    if (!element.empty()) {
        const auto* cText = element.child_value();
        if (cText[0] != '\0') {
            return numeric_conversionComplete(std::string_view{cText}, readerNullVal);
        }
    }
    return readerNullVal;
}

std::string XmlReaderElement::getText() const
{
    if (!element.empty()) {
        return gmlc::utilities::stringOps::trim(element.child_value());
    }
    return "";
}

std::string XmlReaderElement::getMultiText(const std::string& sep) const
{
    std::string ret;
    if (!element.empty()) {
        for (auto childNode = element.first_child(); !childNode.empty();
             childNode = childNode.next_sibling()) {
            if ((childNode.type() == pugi::node_pcdata) || (childNode.type() == pugi::node_cdata)) {
                auto childText = gmlc::utilities::stringOps::trim(childNode.value());
                if (!childText.empty()) {
                    if (ret.empty()) {
                        ret = childText;
                    } else {
                        ret += sep + childText;
                    }
                }
            }
        }
    }
    return ret;
}

bool XmlReaderElement::hasAttribute(const std::string& attributeName) const
{
    if (!element.empty()) {
        return !element.attribute(attributeName.c_str()).empty();
    }
    return false;
}

bool XmlReaderElement::hasElement(const std::string& elementName) const
{
    if (!element.empty()) {
        return !element.child(elementName.c_str()).empty();
    }
    return false;
}

readerAttribute XmlReaderElement::getFirstAttribute()
{
    if (!element.empty()) {
        att = element.first_attribute();
        if (!att.empty()) {
            return {std::string(att.name()), std::string(att.value())};
        }
    }
    return {};
}

readerAttribute XmlReaderElement::getNextAttribute()
{
    if (!att.empty()) {
        att = att.next_attribute();
        if (!att.empty()) {
            return {std::string(att.name()), std::string(att.value())};
        }
    }
    return {};
}

readerAttribute XmlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (!element.empty()) {
        auto attribute = element.attribute(attributeName.c_str());
        if (!attribute.empty()) {
            return {attributeName, std::string(attribute.value())};
        }
    }
    return {};
}

std::string XmlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (!element.empty()) {
        auto attribute = element.attribute(attributeName.c_str());
        if (!attribute.empty()) {
            return {attribute.value()};
        }
    }
    return "";
}

double XmlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!element.empty()) {
        auto attribute = element.attribute(attributeName.c_str());
        if (!attribute.empty()) {
            return numeric_conversionComplete(std::string_view{attribute.value()}, readerNullVal);
        }
    }
    return readerNullVal;
}

std::shared_ptr<readerElement> XmlReaderElement::firstChild() const
{
    pugi::xml_node child;
    if (!element.empty()) {
        child = element.first_child();
        while (!child.empty() && child.type() != pugi::node_element) {
            child = child.next_sibling();
        }
    } else if (isDocument()) {
        child = doc->document_element();
    }
    if (!child.empty()) {
        return std::make_shared<XmlReaderElement>(child, element);
    }
    return nullptr;
}

std::shared_ptr<readerElement> XmlReaderElement::firstChild(const std::string& childName) const
{
    pugi::xml_node child;
    if (!element.empty()) {
        child = element.child(childName.c_str());
    } else if (isDocument()) {
        child = doc->child(childName.c_str());
    }
    if (!child.empty()) {
        return std::make_shared<XmlReaderElement>(child, element);
    }
    return nullptr;
}

void XmlReaderElement::moveToNextSibling()
{
    if (!element.empty()) {
        element = element.next_sibling();
        while (!element.empty() && element.type() != pugi::node_element) {
            element = element.next_sibling();
        }
        att = pugi::xml_attribute();
    }
}

void XmlReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!element.empty()) {
        element = element.next_sibling(siblingName.c_str());
        att = pugi::xml_attribute();
    }
}

void XmlReaderElement::moveToFirstChild()
{
    if (!element.empty()) {
        parent = element;
        att = pugi::xml_attribute();
        element = element.first_child();
        while (!element.empty() && element.type() != pugi::node_element) {
            element = element.next_sibling();
        }
    } else if (isDocument()) {
        element = doc->document_element();
    }
}

void XmlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!element.empty()) {
        parent = element;
        att = pugi::xml_attribute();
        element = element.child(childName.c_str());
    } else if (isDocument()) {
        element = doc->child(childName.c_str());
    }
}

void XmlReaderElement::moveToParent()
{
    if (!parent.empty()) {
        element = parent;
        att = pugi::xml_attribute();
        parent = element.parent();
        if (parent.type() != pugi::node_element) {
            parent = pugi::xml_node();
        }
    }
}

std::shared_ptr<readerElement> XmlReaderElement::nextSibling() const
{
    if (!element.empty()) {
        auto sibling = element.next_sibling();
        while (!sibling.empty() && sibling.type() != pugi::node_element) {
            sibling = sibling.next_sibling();
        }
        if (!sibling.empty()) {
            return std::make_shared<XmlReaderElement>(sibling, parent);
        }
    }
    return nullptr;
}

std::shared_ptr<readerElement>
    XmlReaderElement::nextSibling(const std::string& siblingName) const
{
    if (!element.empty()) {
        auto sibling = element.next_sibling(siblingName.c_str());
        if (!sibling.empty()) {
            return std::make_shared<XmlReaderElement>(sibling, parent);
        }
    }
    return nullptr;
}

void XmlReaderElement::bookmark()
{
    bookmarks.emplace_back(element, parent);
}
void XmlReaderElement::restore()
{
    if (bookmarks.empty()) {
        return;
    }
    element = bookmarks.back().first;
    parent = bookmarks.back().second;
    bookmarks.pop_back();
}
