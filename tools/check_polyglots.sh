#!/bin/sh
set -e
cd "$(dirname "$0")/.."

rc=0
for f in tools/assets/*.bat tools/assets/part1/*.bat; do
    [ -f "$f" ] || continue
    n=$(grep -n '^:; exit$' "$f" | head -1 | cut -d: -f1)
    if [ -z "$n" ]; then echo "SKIP $f (no POSIX half)"; continue; fi
    head -n "$n" "$f" > "${TMPDIR:-/tmp}/polyhalf.$$.sh"
    if err=$(sh -n "${TMPDIR:-/tmp}/polyhalf.$$.sh" 2>&1); then
        echo "OK   $f"
    else
        echo "FAIL $f"
        echo "$err" | sed 's/^/     /'
        rc=1
    fi
    rm -f "${TMPDIR:-/tmp}/polyhalf.$$.sh"
done
exit $rc
