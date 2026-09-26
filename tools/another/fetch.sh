#!/bin/sh
set -eu
cd "$(dirname "$0")"

cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
PART1_URL=$(cfg PART1_URL)
PART1_CACHE=$(cfg PART1_CACHE)

DEST=$(cd ../../saturn/cd/data && pwd)

FORCE=0
case "${1:-}" in -f|--force) FORCE=1 ;; esac

mkdir -p "$PART1_CACHE"

REPO=$(printf '%s\n' "$PART1_URL" \
       | sed -e 's#^https\{0,1\}://github\.com/##' -e 's#/releases/.*$##')

AUTHED=0
if command -v gh >/dev/null 2>&1; then
    if [ -n "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ] || gh auth token >/dev/null 2>&1
    then
        AUTHED=1
    fi
fi

fetch() {
    echo "Part I: fetching $1"
    if [ "$AUTHED" = "1" ]; then
        gh release download -R "$REPO" -p "$1" \
            -O "$PART1_CACHE/$1.part" --clobber
    else
        curl -fsSL -o "$PART1_CACHE/$1.part" "$PART1_URL/$1"
    fi
    mv -f "$PART1_CACHE/$1.part" "$PART1_CACHE/$1"
}

fetch SHA256SUMS

WANT="0.bin"

for f in $WANT; do
    if [ "$FORCE" = "1" ] || [ ! -f "$PART1_CACHE/$f" ]; then
        fetch "$f"
    elif ! ( cd "$PART1_CACHE" &&
             awk -v f="$f" '{ n = $NF; sub(/^\*/, "", n); if (n == f) print }' SHA256SUMS |
             sha256sum -c --status - ) 2>/dev/null; then
        echo "Part I: $f is stale against the published digest"
        fetch "$f"
    fi
done

( cd "$PART1_CACHE" &&
  awk -v want="$WANT" 'BEGIN { n = split(want, w, " ") }
       { f = $NF; sub(/^[*]/, "", f);
         for (i = 1; i <= n; i++) if (f == w[i]) print }' SHA256SUMS |
  sha256sum -c - )

cp -f "$PART1_CACHE/0.bin"       "$DEST/ANOTHER.BIN"

echo "Part I: ANOTHER.BIN $(wc -c < "$DEST/ANOTHER.BIN") bytes"

