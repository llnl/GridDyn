#!/bin/bash
pushd . >/dev/null || exit
cd ..
git clone https://github.com/XBraid/xbraid.git xbraid
cd xbraid || exit

make -j4 CC=mpicc
popd >/dev/null || exit
