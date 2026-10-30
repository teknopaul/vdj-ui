#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=sha256_test

gcc -Wall -Werror -Wno-unused-function -g -O0 \
    $snip_test.c \
    -o $snip_test \
    -lm \
    && ./$snip_test \
    && rm -f ./$snip_test ./${snip_test}.c
