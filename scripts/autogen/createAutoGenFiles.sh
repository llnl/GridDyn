#!/bin/bash
find ../../src/griddyn/ -type f -name "*.h*" -exec sh -c 'echo python griddynAutoCodeGen.py "$1" \> "$(echo "$1" | sed s/.h.*/AutoCodeGen.cpp/)"' __ {} \; | sh -x
find ../../src/griddyn -size 0 -exec rm -rf {} \;
echo "Autogen file created are:"
echo "*************************"
find ../../src/griddyn -name "*AutoCodeGen.cpp"
