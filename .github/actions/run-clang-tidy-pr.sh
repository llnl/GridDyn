#!/bin/bash
set -euo pipefail

ROOT_DIR=$(pwd)

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

file_in_compile_db() {
    local source_file="$1"
    jq -e --arg file "$source_file" 'map(select(.file == $file)) | length > 0' \
        build/compile_commands.json >/dev/null
}

FILES_URL="$(jq -r '.pull_request._links.self.href' "$GITHUB_EVENT_PATH")/files"
FILES=$(curl -s -X GET -G "$FILES_URL" | jq -r '.[] | .filename')
echo "====Files Changed in PR===="
echo "$FILES"
filecount=$(echo "$FILES" | grep -c -E '\.(cpp|cc|cxx|c)$' || true)
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
    append_include_args "${ROOT_DIR}/ThirdParty/suitesparse-cmake/SuiteSparse/KLU/Include"
    append_include_args "${ROOT_DIR}/ThirdParty/suitesparse-cmake/SuiteSparse/AMD/Include"
    append_include_args "${ROOT_DIR}/ThirdParty/suitesparse-cmake/SuiteSparse/BTF/Include"
    append_include_args "${ROOT_DIR}/ThirdParty/suitesparse-cmake/SuiteSparse/COLAMD/Include"
    append_include_args "${ROOT_DIR}/ThirdParty/suitesparse-cmake/SuiteSparse/SuiteSparse_config"
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
        if echo "$line" | grep -E '\.(cpp|cc|cxx|c)$'; then
            source_file="${ROOT_DIR}/${line}"
            if ! file_in_compile_db "$source_file"; then
                echo "skipping ${line}: not present in compilation database"
                continue
            fi
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
