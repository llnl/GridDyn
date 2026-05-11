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
    mElement(xmlElement), mParent(xmlParent)
{
}

XmlReaderElement::~XmlReaderElement() = default;
void XmlReaderElement::clear()
{
    mElement = pugi::xml_node();
    mParent = pugi::xml_node();
    mAttribute = pugi::xml_attribute();
    mBookmarks.clear();
}

bool XmlReaderElement::isValid() const
{
    return (static_cast<bool>(mElement) || (!mParent && mDoc));
}
bool XmlReaderElement::isDocument() const
{
    if (!mParent) {
        if (mDoc) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<readerElement> XmlReaderElement::clone() const
{
    auto ret = std::make_shared<XmlReaderElement>(mElement, mParent);
    ret->mDoc = mDoc;
    return ret;
}

bool XmlReaderElement::loadFile(const std::string& fileName)
{
    mDoc = std::make_shared<pugi::xml_document>();
    const auto res = mDoc->load_file(fileName.c_str());
    clear();
    if (res) {
        mElement = mDoc->document_element();
        return true;
    }
    std::cerr << "unable to load xml file " << fileName << ": " << res.description() << '\n';
    mDoc = nullptr;
    return false;
}

bool XmlReaderElement::parse(const std::string& inputString)
{
    mDoc = std::make_shared<pugi::xml_document>();
    const auto res = mDoc->load_buffer(inputString.data(), inputString.length());
    clear();
    if (res) {
        mElement = mDoc->document_element();
        return true;
    }
    std::cerr << "unable to parse xml string: " << res.description() << '\n';
    mDoc = nullptr;
    return false;
}

std::string XmlReaderElement::getName() const
{
    if (!mElement.empty()) {
        return mElement.name();
    }
    return "";
}

double XmlReaderElement::getValue() const
{
    if (!mElement.empty()) {
        const auto* cText = mElement.child_value();
        if (cText[0] != '\0') {
            return numeric_conversionComplete(std::string_view{cText}, readerNullVal);
        }
    }
    return readerNullVal;
}

std::string XmlReaderElement::getText() const
{
    if (!mElement.empty()) {
        return gmlc::utilities::stringOps::trim(mElement.child_value());
    }
    return "";
}

std::string XmlReaderElement::getMultiText(const std::string& sep) const
{
    std::string ret;
    if (!mElement.empty()) {
        for (auto childNode = mElement.first_child(); !childNode.empty();
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
    if (!mElement.empty()) {
        return !mElement.attribute(attributeName.c_str()).empty();
    }
    return false;
}

bool XmlReaderElement::hasElement(const std::string& elementName) const
{
    if (!mElement.empty()) {
        return !mElement.child(elementName.c_str()).empty();
    }
    return false;
}

readerAttribute XmlReaderElement::getFirstAttribute()
{
    if (!mElement.empty()) {
        mAttribute = mElement.first_attribute();
        if (!mAttribute.empty()) {
            return {std::string(mAttribute.name()), std::string(mAttribute.value())};
        }
    }
    return {};
}

readerAttribute XmlReaderElement::getNextAttribute()
{
    if (!mAttribute.empty()) {
        mAttribute = mAttribute.next_attribute();
        if (!mAttribute.empty()) {
            return {std::string(mAttribute.name()), std::string(mAttribute.value())};
        }
    }
    return {};
}

readerAttribute XmlReaderElement::getAttribute(const std::string& attributeName) const
{
    if (!mElement.empty()) {
        auto attribute = mElement.attribute(attributeName.c_str());
        if (!attribute.empty()) {
            return {attributeName, std::string(attribute.value())};
        }
    }
    return {};
}

std::string XmlReaderElement::getAttributeText(const std::string& attributeName) const
{
    if (!mElement.empty()) {
        auto attribute = mElement.attribute(attributeName.c_str());
        if (!attribute.empty()) {
            return {attribute.value()};
        }
    }
    return "";
}

double XmlReaderElement::getAttributeValue(const std::string& attributeName) const
{
    if (!mElement.empty()) {
        auto attribute = mElement.attribute(attributeName.c_str());
        if (!attribute.empty()) {
            return numeric_conversionComplete(std::string_view{attribute.value()}, readerNullVal);
        }
    }
    return readerNullVal;
}

std::shared_ptr<readerElement> XmlReaderElement::firstChild() const
{
    pugi::xml_node child;
    if (!mElement.empty()) {
        child = mElement.first_child();
        while (!child.empty() && child.type() != pugi::node_element) {
            child = child.next_sibling();
        }
    } else if (isDocument()) {
        child = mDoc->document_element();
    }
    if (!child.empty()) {
        return std::make_shared<XmlReaderElement>(child, mElement);
    }
    return nullptr;
}

std::shared_ptr<readerElement> XmlReaderElement::firstChild(const std::string& childName) const
{
    pugi::xml_node child;
    if (!mElement.empty()) {
        child = mElement.child(childName.c_str());
    } else if (isDocument()) {
        child = mDoc->child(childName.c_str());
    }
    if (!child.empty()) {
        return std::make_shared<XmlReaderElement>(child, mElement);
    }
    return nullptr;
}

void XmlReaderElement::moveToNextSibling()
{
    if (!mElement.empty()) {
        mElement = mElement.next_sibling();
        while (!mElement.empty() && mElement.type() != pugi::node_element) {
            mElement = mElement.next_sibling();
        }
        mAttribute = pugi::xml_attribute();
    }
}

void XmlReaderElement::moveToNextSibling(const std::string& siblingName)
{
    if (!mElement.empty()) {
        mElement = mElement.next_sibling(siblingName.c_str());
        mAttribute = pugi::xml_attribute();
    }
}

void XmlReaderElement::moveToFirstChild()
{
    if (!mElement.empty()) {
        mParent = mElement;
        mAttribute = pugi::xml_attribute();
        mElement = mElement.first_child();
        while (!mElement.empty() && mElement.type() != pugi::node_element) {
            mElement = mElement.next_sibling();
        }
    } else if (isDocument()) {
        mElement = mDoc->document_element();
    }
}

void XmlReaderElement::moveToFirstChild(const std::string& childName)
{
    if (!mElement.empty()) {
        mParent = mElement;
        mAttribute = pugi::xml_attribute();
        mElement = mElement.child(childName.c_str());
    } else if (isDocument()) {
        mElement = mDoc->child(childName.c_str());
    }
}

void XmlReaderElement::moveToParent()
{
    if (!mParent.empty()) {
        mElement = mParent;
        mAttribute = pugi::xml_attribute();
        mParent = mElement.parent();
        if (mParent.type() != pugi::node_element) {
            mParent = pugi::xml_node();
        }
    }
}

std::shared_ptr<readerElement> XmlReaderElement::nextSibling() const
{
    if (!mElement.empty()) {
        auto sibling = mElement.next_sibling();
        while (!sibling.empty() && sibling.type() != pugi::node_element) {
            sibling = sibling.next_sibling();
        }
        if (!sibling.empty()) {
            return std::make_shared<XmlReaderElement>(sibling, mParent);
        }
    }
    return nullptr;
}

std::shared_ptr<readerElement> XmlReaderElement::nextSibling(const std::string& siblingName) const
{
    if (!mElement.empty()) {
        auto sibling = mElement.next_sibling(siblingName.c_str());
        if (!sibling.empty()) {
            return std::make_shared<XmlReaderElement>(sibling, mParent);
        }
    }
    return nullptr;
}

void XmlReaderElement::bookmark()
{
    mBookmarks.emplace_back(mElement, mParent);
}
void XmlReaderElement::restore()
{
    if (mBookmarks.empty()) {
        return;
    }
    mElement = mBookmarks.back().first;
    mParent = mBookmarks.back().second;
    mBookmarks.pop_back();
}
