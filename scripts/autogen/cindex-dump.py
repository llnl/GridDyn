def get_diag_info(diag):
    return {
        "severity": diag.severity,
        "location": diag.location,
        "spelling": diag.spelling,
        "ranges": diag.ranges,
        "fixits": diag.fixits,
    }


def get_cursor_id(cursor, cursor_list=[]):
    if not opts.showIDs:
        return None

    if cursor is None:
        return None

    # FIXME: This is really slow. It would be nice if the index API exposed
    # something that let us hash cursors.
    for i, c in enumerate(cursor_list):
        if cursor == c:
            return i
    cursor_list.append(cursor)
    return len(cursor_list) - 1


def get_info(node, depth=0):
    if opts.maxDepth is not None and depth >= opts.maxDepth:
        children = None
    else:
        children = [get_info(c, depth + 1) for c in node.get_children()]
    return {
        "id": get_cursor_id(node),
        "kind": node.kind,
        "usr": node.get_usr(),
        "spelling": node.spelling,
        "type": node.type.spelling,
        "location": node.location,
        "extent.start": node.extent.start,
        "extent.end": node.extent.end,
        "is_definition": node.is_definition(),
        "definition id": get_cursor_id(node.get_definition()),
        "children": children,
    }


def main():
    import clang.cindex
    from pprint import pprint

    from optparse import OptionParser, OptionGroup

    global opts

    parser = OptionParser("usage: %prog [options] {filename} [clang-args*]")
    parser.add_option(
        "", "--show-ids", dest="showIDs", help="Don't compute cursor IDs (very slow)", default=False
    )
    parser.add_option(
        "",
        "--max-depth",
        dest="maxDepth",
        help="Limit cursor expansion to depth N",
        metavar="N",
        type=int,
        default=None,
    )
    parser.disable_interspersed_args()
    (opts, myargs) = parser.parse_args()

    if len(myargs) == 0:
        parser.error("invalid number arguments")

    index = clang.cindex.Index.create()
    index = clang.cindex.Index.create()

    #    tu = index.parse(myargs, args=["-I/software/griddyn/src/griddyn -I/software/griddyn/src/  -I/software/griddyn/src/utilities -I/software/griddyn/gridDyn -I/software/anaconda2/envs/matlabDyn/include/boost/ -I/software/anaconda2/envs/matlabDyn/gcc/include/c++/ -I/software/anaconda2/envs/matlabDyn/gcc/include/c++/x86_64-pc-linux-gnu/ -I/software/anaconda2/envs/matlabDyn/include","-std=std++11"])
    print(myargs)
    # tu = index.parse(myargs[0], args=[])
    # includes= "-I/software/griddyn/src/griddyn -I/software/griddyn/src/  -I/software/griddyn/src/utilities -I/software/griddyn/gridDyn -I/software/anaconda2/envs/matlabDyn/include/boost/ -I/software/anaconda2/envs/matlabDyn/gcc/include/c++/ -I/software/anaconda2/envs/matlabDyn/gcc/include/c++/x86_64-pc-linux-gnu/ -I/software/anaconda2/envs/matlabDyn/include".split()
    includes = "-I/software/griddyn/src/griddyn -I/software/griddyn/src/  -I/software/griddyn/src/utilities -I/software/griddyn/gridDyn".split()

    tu = index.parse(myargs[0], args=includes + ["-std=c++14"])
    if not tu:
        parser.error("unable to load input")

    pprint(("diags", list(map(get_diag_info, tu.diagnostics))))
    pprint(("nodes", get_info(tu.cursor)))


if __name__ == "__main__":
    main()
