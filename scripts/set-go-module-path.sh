#!/usr/bin/env bash
#
# Derives the Go module path from the repo's actual git remote and
# rewrites go/go.mod (plus the one internal reference to it in
# go/examples/basic_usage.go) to match.
#
# Go module paths are the fetch address `go get` uses, so they can't be
# resolved automatically at build time -- they have to be a fixed string
# in go.mod. This script is the practical substitute: run it once after
# the repo has a real GitHub/GitLab remote, and it reads that remote
# instead of you having to know/type the path yourself.
#
# go.mod lives in go/, not the repo root, so the module path includes
# that subdirectory (github.com/<owner>/<repo>/go) -- that's what makes
# `go get github.com/<owner>/<repo>/go` resolve correctly in a
# multi-language monorepo like this one.
#
# Usage:
#   ./scripts/set-go-module-path.sh              # derive from `git remote get-url origin`
#   ./scripts/set-go-module-path.sh <host>/<owner>/<repo>   # override explicitly

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

derive_from_remote() {
    local url
    url="$(git -C "$ROOT_DIR" remote get-url origin 2>/dev/null)" || {
        echo "error: no 'origin' remote found, and no path given explicitly." >&2
        echo "usage: $0 [<host>/<owner>/<repo>]" >&2
        exit 1
    }

    # Normalize: strip protocol/user, strip trailing .git, convert
    # ssh-style git@host:owner/repo to host/owner/repo.
    url="${url#git@}"
    url="${url#https://}"
    url="${url#http://}"
    url="${url/://}"        # git@host:owner/repo -> host/owner/repo
    url="${url%.git}"
    echo "$url"
}

REPO_PATH="${1:-$(derive_from_remote)}"

# A bare placeholder (no slash, e.g. "multilang") is passed through as-is
# -- that's the no-remote-yet state this repo ships with. Anything that
# looks like a real <host>/<owner>/<repo> gets /go appended, since go.mod
# lives in the go/ subdirectory, not the repo root.
case "$REPO_PATH" in
    */*) MODULE_PATH="${REPO_PATH}/go" ;;
    *) MODULE_PATH="$REPO_PATH" ;;
esac

echo "Setting Go module path to: ${MODULE_PATH}"

CURRENT="$(cd "$ROOT_DIR/go" && go mod edit -json | python3 -c 'import json,sys; print(json.load(sys.stdin)["Module"]["Path"])')"

(cd "$ROOT_DIR/go" && go mod edit -module "$MODULE_PATH")
sed -i "s#\"${CURRENT}\"#\"${MODULE_PATH}\"#" "$ROOT_DIR/go/examples/basic_usage.go"

echo "Updated go/go.mod and go/examples/basic_usage.go (was: ${CURRENT})"
echo "Run 'go build ./...' in go/ to confirm it still compiles."
