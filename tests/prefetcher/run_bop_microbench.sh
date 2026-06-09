#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${TMPDIR:-/tmp}/bop_microbench_test"
compat_include="${TMPDIR:-/tmp}/bop_microbench_include"
extra_cflags=()

if ! printf '#include <malloc.h>\n' | gcc -E -x c - >/dev/null 2>&1; then
  mkdir -p "$compat_include"
  printf '#include <stdlib.h>\n' > "$compat_include/malloc.h"
  extra_cflags=(-I"$compat_include")
fi

gcc -std=gnu11 -Wall -Wextra -I"$repo_root/src" "${extra_cflags[@]}" \
  "$repo_root/tests/prefetcher/bop_microbench_test.c" \
  -o "$out"

"$out"
