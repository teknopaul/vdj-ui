#!/bin/bash
#
# Runs the bash tests of the trakcrate command line tools: src-cli/test/test_*.sh
#
#   src-cli/test/run.sh [test_file.sh ...]
#
set -u
cd "$(dirname "$0")" || exit 1

# the bash commands call the helper
make -s -C ../c >/dev/null || { echo "building the helper failed"; exit 1; }

if [ $# -gt 0 ]; then
    files=("$@")
else
    files=(test_*.sh)
fi

failed=0
for f in "${files[@]}"; do
    if ! bash "./$f"; then
        failed=$((failed + 1))
    fi
done

if [ "$failed" -gt 0 ]; then
    echo "FAILED: $failed test file(s)"
    exit 1
fi
echo "all test files passed"
