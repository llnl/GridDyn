/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <exception>
#include <memory>
#include <string>
#include <string_view>

/** Sentinel returned when a reader cannot provide a numeric value. */
const double readerNullVal(-1.345e48);

/** A name and textual value returned by a ReaderElement attribute query.
 *
 * A default-constructed attribute has an empty name and is invalid. Numeric
 * accessors convert the stored text and return a sentinel when conversion
 * fails.
 */
class ReaderAttribute {
  private:
    std::string mName;
    std::string mText;

  public:
    /** Creates an empty, invalid attribute. */
    ReaderAttribute();

    /** Creates an attribute by taking ownership of its name and text.
     * @param[in] attName attribute name
     * @param[in] attText textual attribute value
     */
    ReaderAttribute(std::string attName, std::string attText);

    /** Creates an attribute by copying the supplied views.
     * @param[in] attName attribute name
     * @param[in] attText textual attribute value
     */
    ReaderAttribute(std::string_view attName, std::string_view attText);

    /** Replaces the name and text.
     * @param[in] attName new attribute name
     * @param[in] attText new textual attribute value
     */
    void set(std::string_view attName, std::string_view attText);

    /** Gets the attribute name. */
    const std::string& getName() const { return mName; }

    /** Gets the unconverted attribute text. */
    const std::string& getText() const { return mText; }

    /** Converts the text to a double, returning readerNullVal on failure. */
    double getValue() const;

    /** Converts the text to a signed 64-bit integer.
     * @return the converted value, or the minimum signed 64-bit value if
     * conversion fails
     */
    int64_t getInt() const;

    /** Tests whether this attribute has a non-empty name. */
    bool isValid() const { return (!mName.empty()); }
};

/** Exception type for carrying a reader parse error message. */
class ElementParseException: public std::exception {
  private:
    std::string mErrorString = "parse error\n";

  public:
    /** Creates an exception with the supplied diagnostic text.
     * @param[in] str parse error description
     */
    explicit ElementParseException(std::string_view str): mErrorString(str) {}

    /** Gets the stored diagnostic text. */
    const char* what() const noexcept override { return mErrorString.c_str(); }
};

/** Backend-neutral cursor for reading hierarchical document data.
 *
 * Concrete readers adapt a document format to common element, attribute, and
 * navigation operations. Navigation methods prefixed with moveTo mutate this
 * cursor; firstChild and nextSibling return reader handles positioned relative
 * to the current element. A reader can also save and restore cursor positions
 * with bookmark and restore.
 */
class ReaderElement {
  public:
    /** Destroys the reader cursor. */
    virtual ~ReaderElement() = 0;

    /** Loads a document from a file.
     * @param[in] fileName path to the input file
     * @return true when the backend successfully loads the file
     */
    virtual bool loadFile(const std::string& fileName) = 0;

    /** Parses input text into a document.
     * The interpretation of inputString is backend-specific.
     * @param[in] inputString document text
     * @return true when the backend successfully parses the input
     */
    virtual bool parse(const std::string& inputString) = 0;

    /** Creates another reader positioned at the same location. */
    virtual std::shared_ptr<ReaderElement> clone() const = 0;

    /** Gets the current element's name. */
    virtual std::string getName() const = 0;

    /** Gets the current element as a number.
     * @return the converted value, or readerNullVal when no numeric value is
     * available
     */
    virtual double getValue() const = 0;

    /** Gets the current element's text value. */
    virtual std::string getText() const = 0;

    /** Gets all text values represented by the current element.
     * @param[in] separator text inserted between multiple values
     * @return the combined text
     */
    virtual std::string getMultiText(const std::string& separator = " ") const = 0;

    /** Checks whether the current element has the named attribute. */
    virtual bool hasAttribute(const std::string& attributeName) const = 0;

    /** Checks whether the current element has a child with the given name. */
    virtual bool hasElement(const std::string& elementName) const = 0;

    /** Starts iteration over the current element's attributes.
     * @return the first attribute, or an invalid ReaderAttribute if there are
     * no attributes
     */
    virtual ReaderAttribute getFirstAttribute() = 0;

    /** Advances the current attribute iteration.
     * @return the next attribute, or an invalid ReaderAttribute at the end
     */
    virtual ReaderAttribute getNextAttribute() = 0;

    /** Gets one attribute by name.
     * @return the attribute, or an invalid ReaderAttribute if it is absent
     */
    virtual ReaderAttribute getAttribute(const std::string& attributeName) const = 0;

    /** Gets an attribute's text value, or an empty string if it is absent. */
    virtual std::string getAttributeText(const std::string& attributeName) const = 0;

    /** Gets an attribute's numeric value, or readerNullVal if it is absent or
     * cannot be converted.
     */
    virtual double getAttributeValue(const std::string& attributeName) const = 0;

    /** Gets a reader positioned at the first child element. */
    virtual std::shared_ptr<ReaderElement> firstChild() const = 0;

    /** Gets a reader positioned at the named child element. */
    virtual std::shared_ptr<ReaderElement> firstChild(const std::string& childName) const = 0;

    /** Moves this cursor to the next sibling element. */
    virtual void moveToNextSibling() = 0;

    /** Moves this cursor to the next sibling with the given name. */
    virtual void moveToNextSibling(const std::string& siblingName) = 0;

    /** Moves this cursor to its first child element. */
    virtual void moveToFirstChild() = 0;

    /** Moves this cursor to the named child element. */
    virtual void moveToFirstChild(const std::string& childName) = 0;

    /** Moves this cursor to its parent element. */
    virtual void moveToParent() = 0;

    /** Gets a reader positioned at the next sibling element. */
    virtual std::shared_ptr<ReaderElement> nextSibling() const = 0;

    /** Gets a reader positioned at the next sibling with the given name. */
    virtual std::shared_ptr<ReaderElement> nextSibling(const std::string& siblingName) const = 0;

    /** Checks whether the current cursor points to a valid element. */
    virtual bool isValid() const = 0;

    /** Checks whether the current cursor represents the document root. */
    virtual bool isDocument() const = 0;

    /** Saves the current location on the reader's bookmark stack. */
    virtual void bookmark() = 0;

    /** Restores and removes the most recently saved location, if present. */
    virtual void restore() = 0;
};
