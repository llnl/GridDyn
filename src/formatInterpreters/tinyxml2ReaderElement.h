#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "readerElement.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tinyxml2 {
class XMLDocument;
class XMLElement;
class XMLAttribute;
}  // namespace tinyxml2

/** @brief class defines a reader element around the ticpp element XML reader*/
class tinyxml2ReaderElement: public readerElement {
  public:
    tinyxml2ReaderElement();
    explicit tinyxml2ReaderElement(const std::string& fileName);

    tinyxml2ReaderElement(const tinyxml2::XMLElement* xmlElement,
                          const tinyxml2::XMLElement* xmlParent);

    virtual ~tinyxml2ReaderElement() override;

    std::shared_ptr<readerElement> clone() const override;

    virtual bool isValid() const override;
    virtual bool isDocument() const override;

    /** brief load the xml from a string instead of a file*/
    virtual bool parse(const std::string& inputString) override;

    virtual bool loadFile(const std::string& fileName) override;
    virtual std::string getName() const override;
    virtual double getValue() const override;
    virtual std::string getText() const override;
    virtual std::string getMultiText(const std::string& sep = " ") const override;

    virtual bool hasAttribute(const std::string& attributeName) const override;
    virtual bool hasElement(const std::string& elementName) const override;

    virtual readerAttribute getFirstAttribute() override;
    virtual readerAttribute getNextAttribute() override;
    virtual readerAttribute getAttribute(const std::string& attributeName) const override;
    virtual std::string getAttributeText(const std::string& attributeName) const override;
    virtual double getAttributeValue(const std::string& attributeName) const override;

    virtual std::shared_ptr<readerElement> firstChild() const override;
    virtual std::shared_ptr<readerElement> firstChild(const std::string& childName) const override;

    virtual void moveToNextSibling() override;
    virtual void moveToNextSibling(const std::string& siblingName) override;

    virtual void moveToFirstChild() override;
    virtual void moveToFirstChild(const std::string& childName) override;

    virtual void moveToParent() override;

    virtual std::shared_ptr<readerElement> nextSibling() const override;
    virtual std::shared_ptr<readerElement>
        nextSibling(const std::string& siblingName) const override;

    virtual void bookmark() override;
    virtual void restore() override;

  private:
    std::shared_ptr<tinyxml2::XMLDocument> doc;  //!< document root
    const tinyxml2::XMLElement* element = nullptr;
    const tinyxml2::XMLAttribute* att = nullptr;
    const tinyxml2::XMLElement* parent = nullptr;
    std::vector<std::pair<const tinyxml2::XMLElement*, const tinyxml2::XMLElement*>> bookmarks;

  private:
    void clear();
};
