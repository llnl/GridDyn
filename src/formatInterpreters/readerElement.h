/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <exception>
#include <memory>
#include <string>

const double readerNullVal(-1.345e48);
/** @brief simple class for containing an attribute */
class readerAttribute {
  private:
    std::string name;
    std::string text;

  public:
    readerAttribute();
    readerAttribute(std::string attName, std::string attText);
    void set(const std::string& attName, const std::string& attText);
    const std::string& getName() const { return name; }
    const std::string& getText() const { return text; }
    double getValue() const;
    int64_t getInt() const;
    bool isValid() const { return (!name.empty()); }
};

class elementParseException: public std::exception {
  private:
    std::string estr = "parse error\n";

  public:
    explicit elementParseException(const std::string& str): estr(str) {}
    explicit elementParseException(const char* str): estr(str) {}
    const char* what() const noexcept override { return estr.c_str(); }
};

/** @brief class for wrapping various document readers for use in the reader function for
 abstracting various file types readerElement is a abstract virtual class requiring an instantiation
 for implementation
*/
class readerElement {
  public:
    virtual ~readerElement() = 0;
    /** @brief load a file into the element
    @param[in] fileName
    @return true if the file was loaded successfully
    */
    virtual bool loadFile(const std::string& fileName) = 0;

    /** @brief parse a string into the element
    @param[in] inputString the string to parse
    @return true if the file was loaded successfully
    */
    virtual bool parse(const std::string& inputString) = 0;
    /** @brief copy to the element to a new shared ptr object*/
    virtual std::shared_ptr<readerElement> clone() const = 0;

    virtual std::string getName() const = 0;
    virtual double getValue() const = 0;
    virtual std::string getText() const = 0;
    /** @brief get all the text in an element even if they are in multiple sections
    @param[in] separator the separator to place between multiple sections of text
    @return all the text in  string*/
    virtual std::string getMultiText(const std::string& separator = " ") const = 0;

    virtual bool hasAttribute(const std::string& attributeName) const = 0;
    virtual bool hasElement(const std::string& elementName) const = 0;
    virtual readerAttribute getFirstAttribute() = 0;
    virtual readerAttribute getNextAttribute() = 0;
    virtual readerAttribute getAttribute(const std::string& attributeName) const = 0;
    virtual std::string getAttributeText(const std::string& attributeName) const = 0;
    virtual double getAttributeValue(const std::string& attributeName) const = 0;

    virtual std::shared_ptr<readerElement> firstChild() const = 0;
    virtual std::shared_ptr<readerElement> firstChild(const std::string& childName) const = 0;

    virtual void moveToNextSibling() = 0;
    virtual void moveToNextSibling(const std::string& siblingName) = 0;

    virtual void moveToFirstChild() = 0;
    virtual void moveToFirstChild(const std::string& childName) = 0;

    virtual void moveToParent() = 0;

    virtual std::shared_ptr<readerElement> nextSibling() const = 0;
    virtual std::shared_ptr<readerElement> nextSibling(const std::string& siblingName) const = 0;
    /** check if the current element is valid
     *@return true if valid false otherwise
     */
    virtual bool isValid() const = 0;
    /** @brief check if the current element is a root document */
    virtual bool isDocument() const = 0;
    /** @brief save the current location */
    virtual void bookmark() = 0;
    /** @brief restore the last bookmarked location */
    virtual void restore() = 0;
};
