#!/bin/bash
# clang-tidy has segfaulted on me trying to do all the files at once; do them
# a directory at a time
# TODO parallelize this?
files=$(cat ../tidy/files.txt)
for f in $files; do
    echo $f
    clang-format -i $f
done

files=$(cat ../tidy/headers.txt)
for f in $files; do
    echo $f
    clang-format -i $f
done
