#!/bin/sh
set -eu
cd "$(dirname "$0")"

cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }

PART1_URL=$(cfg PART1_URL)
REPO=$(printf '%s\n' "$PART1_URL" \
       | sed -e 's#^https\{0,1\}://github\.com/##' -e 's#/releases/.*$##')

if [ -z "$REPO" ]; then
    echo "Part I: cannot read a repository out of PART1_URL=$PART1_URL" >&2
    exit 1
fi

WAIT_SECONDS=${PART1_WAIT_SECONDS:-600}
POLL_SECONDS=15

if ! command -v gh >/dev/null 2>&1; then
    echo "Part I: gh is not installed, skipping the release check"
    exit 0
fi
if [ -z "${GH_TOKEN:-}" ] && [ -z "${GITHUB_TOKEN:-}" ]; then
    echo "Part I: no GH_TOKEN, skipping the release check"
    echo "Part I: set the SUINEVERE_CI_PAT secret to a PAT with contents:write on"
    echo "Part I: $REPO for this to tag and wait rather than assume"
    exit 0
fi

HEAD_SHA=$(gh api "repos/$REPO/commits/main" --jq .sha)
echo "Part I: $REPO main is at $HEAD_SHA"

tag_on_head() {
    gh api "repos/$REPO/tags" --paginate \
        --jq ".[] | select(.commit.sha == \"$HEAD_SHA\") | .name" | head -1
}

released() {
    gh release view "$1" -R "$REPO" --json assets \
        --jq '[.assets[].name] | index("SHA256SUMS")' 2>/dev/null \
        | grep -qv '^null$'
}

run_active() {
    gh run list -R "$REPO" --limit 20 --json headBranch,status \
        --jq "[.[] | select(.headBranch == \"$1\" and .status != \"completed\")]
              | length" 2>/dev/null
}

settled() {
    released "$1" && [ "$(run_active "$1")" = "0" ]
}

EXISTING=$(tag_on_head)

if [ -n "$EXISTING" ] && settled "$EXISTING"; then
    echo "Part I: $EXISTING already points at main and has been released"
    exit 0
fi

if [ -n "$EXISTING" ]; then
    echo "Part I: $EXISTING points at main but has no published release"
    TAG="$EXISTING"
else
    LATEST=$(gh api "repos/$REPO/tags" --paginate --jq '.[].name' \
             | grep '^v[0-9]\+\.[0-9]\+\.[0-9]\+$' \
             | sort -t. -k1.2,1n -k2,2n -k3,3n | tail -1)
    if [ -z "$LATEST" ]; then
        echo "Part I: no vX.Y.Z tag to count from in $REPO" >&2
        exit 1
    fi

    MAJOR=$(printf '%s\n' "$LATEST" | sed 's/^v\([0-9]*\)\..*/\1/')
    MINOR=$(printf '%s\n' "$LATEST" | sed 's/^v[0-9]*\.\([0-9]*\)\..*/\1/')
    TAG="v$MAJOR.$((MINOR + 1)).0"

    echo "Part I: main is untagged, tagging it $TAG (after $LATEST)"
    gh api "repos/$REPO/git/refs" -X POST \
        -f "ref=refs/tags/$TAG" -f "sha=$HEAD_SHA" >/dev/null
fi

echo "Part I: waiting up to ${WAIT_SECONDS}s for $TAG to publish"

WAITED=0
while [ "$WAITED" -lt "$WAIT_SECONDS" ]; do
    if settled "$TAG"; then
        echo "Part I: $TAG published after ${WAITED}s"
        exit 0
    fi
    sleep "$POLL_SECONDS"
    WAITED=$((WAITED + POLL_SECONDS))
done

echo "Part I: $TAG did not publish within ${WAIT_SECONDS}s" >&2
echo "Part I: check $REPO's own workflow before retrying this build" >&2
exit 1
