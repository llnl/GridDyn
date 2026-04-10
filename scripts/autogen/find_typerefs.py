#!/usr/bin/env python
""" Usage: call with <filename> <typename>
"""

import sys
import clang.cindex

import pdb


def find_typerefs(node, typename):
    """ Find all references to the type named 'typename'
    """
    print(node.spelling)
    print(node.kind)
    print(node.location)
    pdb.set_trace()
    if node.kind.is_reference():
        ref_node = node.referenced
        if ref_node.spelling == typename:
            print(
                "Found %s [line=%s, col=%s]"
                % (
                    typename,
                    node.location.line,
                    node.location.column,
                )
            )
    # Recurse for children of this node
    for c in node.get_children():
        find_typerefs(c, typename)
        print(c.spelling)


index = clang.cindex.Index.create()
tu = index.parse(sys.argv[1])
print("Translation unit:", tu.spelling)
find_typerefs(tu.cursor, sys.argv[2])
