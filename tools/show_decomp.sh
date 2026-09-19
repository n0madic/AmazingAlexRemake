#!/usr/bin/env bash
# Print decompiled C files with declarations/warnings stripped for quick reading.
# Usage: tools/show_decomp.sh <file.c>...
for f in "$@"; do
  echo "=================== $(basename "$f")"
  grep -v -E '^\s*(/\* WARNING|undefined|int |uint |float |double |char |short |ushort |byte |bool |long |void |b2[A-Za-z0-9_]+ \*|[A-Za-z_][A-Za-z0-9_:<>]* \*?[a-zA-Z_][A-Za-z0-9_]* *(\[[0-9]+\])? *;$)' "$f" | grep -v -E '^\s*(undefined[0-9]? |int |float |uint )' | sed -E 's/\(undefined4 \*\)//g; s/\(undefined1 \*\)//g'
done
