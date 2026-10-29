# Reader element interface

GridDyn's format readers present hierarchical input as a common tree cursor.
The abstract `ReaderElement` interface lets file-loading code inspect names,
text, attributes, children, and siblings without depending on one parser
library. The concrete adapters are `XmlReaderElement`, `JsonReaderElement`,
`TomlReaderElement`, `YamlReaderElement`, and `IniReaderElement`.

For the user-facing GridDyn XML schema and examples, see
[GridDyn XML inputs](../xml-input.md). This page describes the reader layer
used by the C++ input code.

## Cursor operations

After a successful load or parse, the reader is positioned at the document
root. `getName`,
`getText`, and `getValue` inspect that location. `getValue` and
`getAttributeValue` return `readerNullVal` when no numeric value is available;
check this sentinel before using a converted number.

`hasElement` and `hasAttribute` test for named children and attributes. A
reader attribute is a name/text pair, and `getFirstAttribute` followed by
`getNextAttribute` iterates the attributes of the current element. Attribute
iteration is stateful on that reader.

There are two ways to navigate:

- `moveToFirstChild`, `moveToNextSibling`, and `moveToParent` change the
  current reader's position.
- `firstChild` and `nextSibling` return another reader handle positioned at
  the requested relative location.

When a child or sibling does not exist, a backend may return `nullptr` or an
invalid reader handle. Check both the pointer and `isValid()` before
dereferencing a returned handle. `isDocument()` identifies the root location.
`bookmark()` saves the current position; `restore()` returns to the most
recent saved position and removes that bookmark.

`clone()` creates another reader at the same location. Concrete readers retain
the parsed document while keeping cursor state separate, so advancing one
reader does not advance its clone.

## Backend differences

| Reader              | Input and traversal notes                                                                                                                                                                                                            |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `XmlReaderElement`  | Reads XML files or text. Element text and XML attributes map directly to the reader operations. A reader constructed from an existing pugixml node is a non-owning view; keep its document alive for the reader's lifetime.          |
| `JsonReaderElement` | Reads JSON files or text. `parse` treats its argument as a file path when that path can be opened, otherwise as JSON text. Scalar object values are exposed as attributes, while objects and arrays are traversed as child elements. |
| `TomlReaderElement` | Reads TOML files or text. Tables and arrays are exposed through child and sibling navigation.                                                                                                                                        |
| `YamlReaderElement` | Reads YAML files or text. Mapping entries and sequence values are exposed through child and sibling navigation.                                                                                                                      |
| `IniReaderElement`  | Loads INI files. Sections appear as one level of child elements and entries as attributes. `parse` is unsupported and returns `false`.                                                                                               |

The shared interface does not erase these format differences. In particular,
callers should check the return value of `loadFile` or `parse`, then check
`isValid()` before reading values. File-name constructors attempt to load the
file but do not return a success flag.

The JSON, TOML, and YAML adapters use small value wrappers to retain the
current value, its reader-visible name, and array/sequence traversal indices.
These wrappers are implementation details of their corresponding readers.
