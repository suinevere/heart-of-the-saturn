#!/bin/sh
set -eu
cd "$(dirname "$0")"

cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
PART1_URL=$(cfg PART1_URL)
PART1_CACHE=$(cfg PART1_CACHE)
PART1_GAME_URL=$(cfg PART1_GAME_URL)
PART1_GAME_MD5=$(cfg PART1_GAME_MD5)

DEST=$(cd ../../saturn/cd/data && pwd)
PART1_KIT_DEST="../assets/part1"

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

WANT="0.bin data.bat"

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

mkdir -p "$PART1_KIT_DEST"
cp -f "$PART1_CACHE/data.bat" "$PART1_KIT_DEST/data.bat"
{
    printf '# Written by tools/another/fetch.sh. Edit tools/another/CONFIG.ME\n'
    printf '# instead -- this file is regenerated on every Part I fetch.\n'
    printf '#\n'
    printf '# DATA_DIR takes three ../ rather than upstream two: data.bat is\n'
    printf '# staged one level deeper here, at tools/assets/part1/.\n'
    printf 'GAME_URL=%s\n' "$PART1_GAME_URL"
    printf 'GAME_MD5=%s\n' "$PART1_GAME_MD5"
    printf 'DATA_DIR=../../../saturn/cd/data\n'
} > "$PART1_KIT_DEST/CONFIG.ME"

echo "Part I: staged data step into $(cd "$PART1_KIT_DEST" && pwd)"
