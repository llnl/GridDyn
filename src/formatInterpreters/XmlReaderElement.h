/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "readerElement.h"
#include <memory>
#include <pugixml.hpp>
#include <string>
#include <utility>
#include <vector>

/** @brief class defines a reader element backed by pugixml. */
class XmlReaderElement: public readerElement {
  public:
    XmlReaderElement();
    explicit XmlReaderElement(const std::string& fileName);

    XmlReaderElement(pugi::xml_node xmlElement, pugi::xml_node xmlParent);

    ~XmlReaderElement() override;

    std::shared_ptr<readerElement> clone() const override;

    bool isValid() const override;
    bool isDocument() const override;

    /** brief load the xml from a string instead of a file*/
    bool parse(const std::string& inputString) override;

    bool loadFile(const std::string& fileName) override;
    std::string getName() const override;
    double getValue() const override;
    std::string getText() const override;
    std::string getMultiText(const std::string& sep = " ") const override;

    bool hasAttribute(const std::string& attributeName) const override;
    bool hasElement(const std::string& elementName) const override;

    readerAttribute getFirstAttribute() override;
    readerAttribute getNextAttribute() override;
    readerAttribute getAttribute(const std::string& attributeName) const override;
    std::string getAttributeText(const std::string& attributeName) const override;
    double getAttributeValue(const std::string& attributeName) const override;

    std::shared_ptr<readerElement> firstChild() const override;
    std::shared_ptr<readerElement> firstChild(const std::string& childName) const override;

    void moveToNextSibling() override;
    void moveToNextSibling(const std::string& siblingName) override;

    void moveToFirstChild() override;
    void moveToFirstChild(const std::string& childName) override;

    void moveToParent() override;

    std::shared_ptr<readerElement> nextSibling() const override;
    std::shared_ptr<readerElement> nextSibling(const std::string& siblingName) const override;

    void bookmark() override;
    void restore() override;

  private:
    std::shared_ptr<pugi::xml_document> doc;  //!< document root
    pugi::xml_node element;
    pugi::xml_attribute att;
    pugi::xml_node parent;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> bookmarks;

  private:
    void clear();
};
