#!/bin/bash
find ../../src/griddyn/ -type f -name "*.h*" -exec sh -c 'echo python griddynAutoCodeGen.py "$1" \> "$(echo "$1" | sed s/.h.*/AutoCodeGen.cpp/)"' __ {} \; | sh -x
# Remove only empty generated outputs; leave unrelated empty source files intact.
find ../../src/griddyn -type f -name "*AutoCodeGen.cpp" -size 0 -exec rm -f {} +
echo "Autogen file created are:"
echo "*************************"
find ../../src/griddyn -name "*AutoCodeGen.cpp"
