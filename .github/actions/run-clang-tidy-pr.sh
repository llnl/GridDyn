#!/bin/bash
set -euo pipefail

append_include_args() {
    local raw_paths="$1"
    local split_paths
    IFS=';' read -r -a split_paths <<<"$raw_paths"
    for include_dir in "${split_paths[@]}"; do
        if [[ -n "$include_dir" && -d "$include_dir" ]]; then
            TIDY_EXTRA_ARGS+=("--extra-arg=-I$include_dir")
        fi
    done
}

FILES_URL="$(jq -r '.pull_request._links.self.href' "$GITHUB_EVENT_PATH")/files"
FILES=$(curl -s -X GET -G "$FILES_URL" | jq -r '.[] | .filename')
echo "====Files Changed in PR===="
echo "$FILES"
filecount=$(echo "$FILES" | grep -c -E '\.(cpp|hpp|c|h)$' || true)
echo "Total changed: $filecount"
tidyerr=0
if ((filecount > 0 && filecount <= 20)); then
    echo "====Configure CMake===="
    mkdir build && cd build || exit
    cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DGRIDDYN_BUILD_C_SHARED_LIBRARY=ON ..
    TIDY_EXTRA_ARGS=()
    KLU_INCLUDE_DIR=$(grep '^KLU_INCLUDE_DIR:' CMakeCache.txt | cut -d= -f2- || true)
    append_include_args "$KLU_INCLUDE_DIR"
    KLU_INCLUDE_DIRS=$(grep '^KLU_INCLUDE_DIRS:' CMakeCache.txt | cut -d= -f2- || true)
    append_include_args "$KLU_INCLUDE_DIRS"
    SUITESPARSE_INCLUDE_DIRS=$(grep '^SuiteSparse_INCLUDE_DIRS:' CMakeCache.txt | cut -d= -f2- || true)
    append_include_args "$SUITESPARSE_INCLUDE_DIRS"
    cd ..
    echo "====Run clang-tidy===="
    if command -v clang-tidy >/dev/null 2>&1; then
        TIDY_CMD=(clang-tidy)
    elif [[ -x /usr/bin/clang-tidy ]]; then
        TIDY_CMD=(/usr/bin/clang-tidy)
    elif [[ -f /usr/share/clang/run-clang-tidy.py ]]; then
        TIDY_CMD=(python3 /usr/share/clang/run-clang-tidy.py)
    else
        echo "clang-tidy not found in PATH and /usr/share/clang/run-clang-tidy.py is unavailable"
        exit 2
    fi
    while read -r line; do
        if echo "$line" | grep -E '\.(cpp|hpp|c|h)$'; then
            if "${TIDY_CMD[@]}" -p build -quiet "${TIDY_EXTRA_ARGS[@]}" "$line"; then
                rc=0
            else
                rc=$?
            fi
            echo "clang-tidy exit code: $rc"
            if [[ "$rc" != "0" ]]; then
                tidyerr=1
            fi
        fi
    done <<<"$FILES"
fi
exit $tidyerr
