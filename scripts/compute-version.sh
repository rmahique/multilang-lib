#!/usr/bin/env bash
# Shared version-computation logic for every language's packaging scripts
# (python/packaging/build-{deb,rpm}.sh, go/packaging/..., etc).
#
# Every commit gets a buildable package, not just tagged releases:
#   - HEAD is an exact git tag  -> that tag is a "release"; use it verbatim.
#   - otherwise                 -> <latest reachable tag, or the caller's
#                                   fallback version if no tag exists yet>
#                                   plus today's UTC date, so untagged
#                                   builds are still uniquely identifiable
#                                   and sort after the base version.
#
# Usage: compute-version.sh <deb|rpm> <fallback-version>
#
# <fallback-version> is whatever this language's own manifest says (e.g.
# pyproject.toml's version, package.json's version, composer.json's
# version) -- this script has no opinion on where that number lives, since
# every language keeps it in a different file.
#
# The date separator differs by package format: Debian's Version field
# allows `+`; RPM's does not (RPM reserves `-` and disallows `+`/`~` isn't
# quite right either -- `^` is what modern rpm, >=4.15, treats as "newer
# than the release it's attached to", which is exactly the semantics an
# unreleased dated build needs).
set -euo pipefail

FORMAT="${1:?usage: compute-version.sh <deb|rpm> <fallback-version>}"
FALLBACK_VERSION="${2:?usage: compute-version.sh <deb|rpm> <fallback-version>}"
TODAY="$(date -u +%Y%m%d)"

BASE_VERSION=""
IS_RELEASE=0

if git rev-parse --git-dir >/dev/null 2>&1; then
    if TAG="$(git describe --tags --exact-match 2>/dev/null)"; then
        BASE_VERSION="${TAG#v}"
        IS_RELEASE=1
    elif TAG="$(git describe --tags --abbrev=0 2>/dev/null)"; then
        BASE_VERSION="${TAG#v}"
    fi
fi

if [ -z "$BASE_VERSION" ]; then
    BASE_VERSION="$FALLBACK_VERSION"
fi

if [ "$IS_RELEASE" -eq 1 ]; then
    echo "$BASE_VERSION"
    exit 0
fi

case "$FORMAT" in
    deb) echo "${BASE_VERSION}+${TODAY}" ;;
    rpm) echo "${BASE_VERSION}^${TODAY}" ;;
    *)
        echo "error: unknown format '$FORMAT' (expected 'deb' or 'rpm')" >&2
        exit 1
        ;;
esac
