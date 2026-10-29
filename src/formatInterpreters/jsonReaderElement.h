/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "nlohmann/json.hpp"
#include "readerElement.h"
#include <memory>
#include <string>
#include <vector>

class JsonElement;

/** ReaderElement adapter for JSON documents using nlohmann::json.
 *
 * JSON object fields are exposed as attributes or child elements according to
 * their value shape; array elements are traversed as repeated values.
 */
class JsonReaderElement: public ReaderElement {
  public:
    /** Creates an empty JSON reader. */
    JsonReaderElement();

    /** Loads a JSON file.
     * Check isValid() after construction to determine whether loading
     * succeeded.
     * @param[in] fileName path to the JSON file
     */
    explicit JsonReaderElement(const std::string& fileName);

    std::shared_ptr<ReaderElement> clone() const override;

    virtual bool isValid() const override;
    virtual bool isDocument() const override;

    virtual bool loadFile(const std::string& fileName) override;
    /** Parses JSON text, or loads it as a file if inputString names an
     * readable file.
     * @param[in] inputString JSON text or a path to a JSON file
     * @return true when the input is valid JSON
     */
    bool parse(const std::string& inputString) override;
    virtual std::string getName() const override;
    virtual double getValue() const override;
    virtual std::string getText() const override;
    virtual std::string getMultiText(const std::string& sep = " ") const override;

    virtual bool hasAttribute(const std::string& attributeName) const override;
    virtual bool hasElement(const std::string& elementName) const override;

    virtual ReaderAttribute getFirstAttribute() override;
    virtual ReaderAttribute getNextAttribute() override;
    virtual ReaderAttribute getAttribute(const std::string& attributeName) const override;
    virtual std::string getAttributeText(const std::string& attributeName) const override;
    virtual double getAttributeValue(const std::string& attributeName) const override;

    virtual std::shared_ptr<ReaderElement> firstChild() const override;
    virtual std::shared_ptr<ReaderElement> firstChild(const std::string& childName) const override;

    virtual void moveToNextSibling() override;
    virtual void moveToNextSibling(const std::string& siblingName) override;

    virtual void moveToFirstChild() override;
    virtual void moveToFirstChild(const std::string& childName) override;

    virtual void moveToParent() override;

    virtual std::shared_ptr<ReaderElement> nextSibling() const override;
    virtual std::shared_ptr<ReaderElement>
        nextSibling(const std::string& siblingName) const override;

    virtual void bookmark() override;
    virtual void restore() override;

  private:
    void clear();

  private:
    std::shared_ptr<nlohmann::json> mDoc;  //!< document root
    std::vector<std::shared_ptr<JsonElement>> mParents;
    std::shared_ptr<JsonElement> mCurrent;
    int mIteratorCount = 0;

    std::vector<std::shared_ptr<JsonReaderElement>> mBookmarks;
};
