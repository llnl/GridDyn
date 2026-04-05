#!/bin/bash
# clang-tidy has segfaulted on me trying to do all the files at once; do them
# a file at a time
# TODO parallelize this?
filename=../tidy/$(basename $(pwd))_$(date +%m%d%y%H%M).tidy
echo "Writing to $filename"
files=$(cat ../tidy/files.txt)
for f in $files; do
	echo $f
	clang-tidy $f >>$filename
done
