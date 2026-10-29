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

/** ReaderElement adapter for XML documents backed by pugixml.
 *
 * Element text, attributes, and child elements are exposed through the common
 * reader interface. A reader created from an existing pugixml node is a
 * non-owning view and must not outlive the document that owns the node.
 */
class XmlReaderElement: public ReaderElement {
  public:
    /** Creates an empty XML reader. */
    XmlReaderElement();

    /** Loads an XML file.
     * Check isValid() after construction to determine whether loading
     * succeeded.
     * @param[in] fileName path to the XML file
     */
    explicit XmlReaderElement(const std::string& fileName);

    /** Creates a reader view over an existing XML node.
     * @param[in] xmlElement node represented by this reader
     * @param[in] xmlParent parent node used for navigation
     */
    XmlReaderElement(pugi::xml_node xmlElement, pugi::xml_node xmlParent);

    /** Releases this reader's document and cursor handles. */
    ~XmlReaderElement() override;

    std::shared_ptr<ReaderElement> clone() const override;

    bool isValid() const override;
    bool isDocument() const override;

    /** Parses XML text instead of loading a file.
     * @param[in] inputString XML document text
     * @return true when the text is valid XML
     */
    bool parse(const std::string& inputString) override;

    bool loadFile(const std::string& fileName) override;
    std::string getName() const override;
    double getValue() const override;
    std::string getText() const override;
    std::string getMultiText(const std::string& sep = " ") const override;

    bool hasAttribute(const std::string& attributeName) const override;
    bool hasElement(const std::string& elementName) const override;

    ReaderAttribute getFirstAttribute() override;
    ReaderAttribute getNextAttribute() override;
    ReaderAttribute getAttribute(const std::string& attributeName) const override;
    std::string getAttributeText(const std::string& attributeName) const override;
    double getAttributeValue(const std::string& attributeName) const override;

    std::shared_ptr<ReaderElement> firstChild() const override;
    std::shared_ptr<ReaderElement> firstChild(const std::string& childName) const override;

    void moveToNextSibling() override;
    void moveToNextSibling(const std::string& siblingName) override;

    void moveToFirstChild() override;
    void moveToFirstChild(const std::string& childName) override;

    void moveToParent() override;

    std::shared_ptr<ReaderElement> nextSibling() const override;
    std::shared_ptr<ReaderElement> nextSibling(const std::string& siblingName) const override;

    void bookmark() override;
    void restore() override;

  private:
    std::shared_ptr<pugi::xml_document> mDoc;  //!< document root
    pugi::xml_node mElement;
    pugi::xml_attribute mAttribute;
    pugi::xml_node mParent;
    std::vector<std::pair<pugi::xml_node, pugi::xml_node>> mBookmarks;

  private:
    void clear();
};
