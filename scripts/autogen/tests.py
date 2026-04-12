import clang.cindex
import sys


def find_decl_ref_expr(node):
    for c in node.get_children():
        #        if c.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR:
        if c.kind == clang.cindex.CursorKind.DECL_REF_EXPR:
            print("Member function call via", c.spelling, c.type.spelling, c.displayname)
        else:
            find_decl_ref_expr(c)


def called_from(node):
    for c in node.get_children():
        if c.kind == clang.cindex.CursorKind.MEMBER_REF_EXPR:
            # if c.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR:
            find_decl_ref_expr(c)


def walk(node):
    if node.kind == clang.cindex.CursorKind.CALL_EXPR:
        #    if node.kind == clang.cindex.CursorKind.FIELD_DECL:
        print("name: %s, type: %s" % (node.spelling or node.displayname, node.type.spelling))
    called_from(node)

    for c in node.get_children():
        walk(c)


def dump_children(node):

    for c in node.get_children():
        print(c.kind, c.spelling, c.type.spelling)
        dump_children(c)


index = clang.cindex.Index.create()
# walk(index.parse(sys.argv[1]).cursor)
includes = "-I/software/griddyn/src/griddyn -I/software/griddyn/src/  -I/software/griddyn/src/utilities -I/software/griddyn/gridDyn".split()

tu = index.parse(sys.argv[1], args=includes + ["-std=c++14"])
walk(tu.cursor)

dump_children(
    index.parse(
        sys.argv[1],
        args=[
            "-I/software/griddyn/src/griddyn -I/software/griddyn/src/  -I/software/griddyn/src/utilities -I/software/griddyn/gridDyn -I/software/anaconda2/envs/matlabDyn/include/boost/ -I/software/anaconda2/envs/matlabDyn/gcc/include/c++/ -I/software/anaconda2/envs/matlabDyn/gcc/include/c++/x86_64-pc-linux-gnu/ -I/software/anaconda2/envs/matlabDyn/include",
            "-std=c++14",
        ],
    ).cursor
)
