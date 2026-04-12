#!/bin/bash

set -euo pipefail

pushd . >/dev/null || exit
cd ..

if [[ ! -d xbraid ]]; then
    git clone https://github.com/XBraid/xbraid.git xbraid
fi

cd xbraid || exit

if command -v mpicc >/dev/null 2>&1; then
    mpi_cc=mpicc
elif command -v mpiicc >/dev/null 2>&1; then
    mpi_cc=mpiicc
else
    echo "No MPI C compiler wrapper found (expected mpicc or mpiicc)" >&2
    exit 1
fi

make -C braid -j4 CC="${mpi_cc}" MPICC="${mpi_cc}"

popd >/dev/null || exit
