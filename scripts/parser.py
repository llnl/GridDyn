# /*
#  * LLNS Copyright Start
#  * Copyright (c) 2014-2018, Lawrence Livermore National Security
#  * This work was performed under the auspices of the U.S. Department
#  * of Energy by Lawrence Livermore National Laboratory in part under
#  * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
#  * Produced at the Lawrence Livermore National Laboratory.
#  * All rights reserved.
#  * For details, see the LICENSE file.
#  * LLNS Copyright End
#  */

import re


def is_start(line):
    """Determine whether a line is the start of a clang-format warning message

    line: string matching the regex below

    return: bool
    """

    # (path):(line):(begin): warning: (warning) [(category)]
    # non-start lines do not contain a leading /, however notes do
    # so we need to disambiguate on both leading / and "warning:"

    # return whether a begins with b
    def match_beginning(a, b):
        return a[: len(b)] == b

    flag = True
    flag = flag and match_beginning(line, "/")
    flag = flag and line.split(" ")[1] == "warning:"
    return flag


class Chunk(object):
    """Representation of a specific clang-format warning"""

    # Regex to parse the warning line of a given message
    # 'FILE:NUM:NUM: WARN STRING [TYPE]'
    FILE = r"(\S+)"
    NUM = r"(\d+)"
    WARN = r"warning:"
    STRING = r"(.+)"
    TYPE = r"(\S+)"

    REGEX = re.compile(
        r"{}:{}:{}: {} {} \[{}\]".format(
            #                    1  2  3      4    5
            FILE,
            NUM,
            NUM,
            WARN,
            STRING,
            TYPE,
        )
    )

    def __init__(self, lines):
        """Construct a chunk given all of its lines

        lines: list of strings, where lines[0] satisfies is_start
        """
        header = lines[0]
        assert is_start(header), "First line of chunk not an actual header"
        parse = Chunk.REGEX.match(header)
        self._file_name = parse.group(1)
        self._line_num = parse.group(2)
        self._position = parse.group(3)
        self._warning = parse.group(4)
        self._guideline = parse.group(5)

        # delete final newline
        self.original = "\n".join(lines)[:-1]

    @property
    def file_name(self):
        """The filename of the file the warning was produced for"""
        return self._file_name

    @property
    def line_num(self):
        """The line number of the line the warning was produced for"""
        return self._line_num

    @property
    def position(self):
        """The character position of the warning"""
        return self._position

    @property
    def warning(self):
        """The complete text of the warning issued"""
        return self._warning

    @property
    def guideline(self):
        """The c++ guideline violated by the warning
        (https://clang.llvm.org/extra/clang-tidy/checks)
        """
        return self._guideline

    def __eq__(self, other):
        return hash(self) == hash(other)

    def __hash__(self):
        # TODO this probably shouldn't take line_num into account
        # drop everything in the path before 'GridDyn'
        filename = self.file_name.split("/")
        index = filename.index("GridDyn")
        filename = "/".join(filename[index:])
        return hash((filename, self.line_num, self.position, self.warning, self.guideline))

    def __str__(self):
        return "{}:{} [{}]".format(self.file_name, self.line_num, self.guideline)

    def __repr__(self):
        return "<{}>".format(str(self))
