#!/usr/bin/env bash
#
# Runs each port's unit test suite (SQLite/filesystem, no live DB needed)
# inside a disposable, single-use container -- never directly against the
# host's Python/Node/PHP/Go/C toolchain. Same principle as
# run-live-db-tests.sh, applied to the fast local suite too: every test
# run is reproducible regardless of what's installed on the machine
# running it, and leaves nothing behind on the host once the container
# exits.
#
# Reuses each port's existing conformance/Dockerfile.conformance image
# (already has every dependency installed) and overrides its
# live-DB-only ENTRYPOINT to run that port's plain unit test command
# instead -- no network, no database containers, no env vars needed.
#
# Every version this script depends on is a variable, never hardcoded:
#
#   PYTHON_VERSION   default: 3.11   (Dockerfile build ARG)
#   NODE_VERSION     default: 20     (Dockerfile build ARG)
#   PHP_VERSION      default: 8.2    (Dockerfile build ARG)
#   GO_VERSION       default: 1.23   (Dockerfile build ARG)
#   DEBIAN_VERSION   default: bookworm (Dockerfile build ARG, C/C++ image)
#
# Container engine is auto-detected (docker preferred, podman as
# fallback) or forced with:
#
#   CONTAINER_ENGINE   docker | podman
#
# Usage:
#   ./run-unit-tests.sh                 # all ports
#   ./run-unit-tests.sh python go       # only these ports
#   PYTHON_VERSION=3.12 ./run-unit-tests.sh python
#
# Exit code is non-zero if any port's suite fails. Every container is
# --rm: it never outlives its own test run, on success or failure.

set -u
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PYTHON_VERSION="${PYTHON_VERSION:-3.11}"
NODE_VERSION="${NODE_VERSION:-20}"
PHP_VERSION="${PHP_VERSION:-8.2}"
GO_VERSION="${GO_VERSION:-1.23}"
DEBIAN_VERSION="${DEBIAN_VERSION:-bookworm}"

# --- container engine detection -------------------------------------------

if [ -n "${CONTAINER_ENGINE:-}" ]; then
    : # honor explicit override
elif docker info >/dev/null 2>&1; then
    CONTAINER_ENGINE="docker"
elif command -v podman >/dev/null 2>&1; then
    CONTAINER_ENGINE="podman"
else
    echo "error: no working container engine found (tried docker, podman)" >&2
    exit 1
fi
echo "Using container engine: ${CONTAINER_ENGINE}"

# --- which ports to test ----------------------------------------------------

ALL_PORTS=(python javascript php go c)
if [ "$#" -gt 0 ]; then
    PORTS=("$@")
else
    PORTS=("${ALL_PORTS[@]}")
fi

# --- per-port image build (shared image with run-live-db-tests.sh) --------

build_image() {
    local port="$1" dockerfile="$2" build_arg="$3"
    "$CONTAINER_ENGINE" build \
        --build-arg "$build_arg" \
        -f "$ROOT_DIR/$dockerfile" \
        -t "multilang-conformance-${port}" \
        "$ROOT_DIR"
}

image_for() {
    case "$1" in
        python) build_image python python/Dockerfile.conformance "PYTHON_VERSION=${PYTHON_VERSION}" ;;
        javascript) build_image javascript javascript/Dockerfile.conformance "NODE_VERSION=${NODE_VERSION}" ;;
        php) build_image php php/Dockerfile.conformance "PHP_VERSION=${PHP_VERSION}" ;;
        go) build_image go go/Dockerfile.conformance "GO_VERSION=${GO_VERSION}" ;;
        c) build_image c c/Dockerfile.conformance "DEBIAN_VERSION=${DEBIAN_VERSION}" ;;
    esac
}

echo ""
echo "Building test images..."
for port in "${PORTS[@]}"; do
    echo "--- building multilang-conformance-${port} ---"
    image_for "$port" || { echo "FAILED to build image for ${port}" >&2; exit 1; }
done

# --- per-port unit test command, overriding the live-DB-only ENTRYPOINT ---
# WORKDIR is already set correctly inside each image (see the matching
# Dockerfile.conformance), so only the command needs overriding.

run_port() {
    local port="$1"
    case "$port" in
        python)
            "$CONTAINER_ENGINE" run --rm --entrypoint pytest \
                "multilang-conformance-python" tests/ -v
            ;;
        javascript)
            "$CONTAINER_ENGINE" run --rm --entrypoint npm \
                "multilang-conformance-javascript" test
            ;;
        php)
            "$CONTAINER_ENGINE" run --rm --entrypoint vendor/bin/phpunit \
                "multilang-conformance-php"
            ;;
        go)
            # conformance.test is one binary covering every _test.go file
            # in the package (validation, strings, conformance) -- built
            # once at image-build time, see go/Dockerfile.conformance.
            "$CONTAINER_ENGINE" run --rm --entrypoint ./conformance.test \
                "multilang-conformance-go" -test.v
            ;;
        c)
            # The image only prebuilds test_conformance/test_cpp (the
            # live-DB binaries); `make test` here builds the remaining
            # test_validation/test_strings binaries too and runs all four.
            "$CONTAINER_ENGINE" run --rm --entrypoint sh \
                "multilang-conformance-c" -c "make test"
            ;;
    esac
}

# --- run everything --------------------------------------------------------------

declare -A RESULTS
FAILED=0

for port in "${PORTS[@]}"; do
    echo ""
    echo "=== ${port} (unit tests, SQLite/filesystem, disposable container) ==="
    if run_port "$port"; then
        RESULTS["$port"]="PASS"
    else
        RESULTS["$port"]="FAIL"
        FAILED=1
    fi
done

# --- summary -----------------------------------------------------------------

echo ""
echo "=== Summary ==="
for port in "${PORTS[@]}"; do
    printf "%-12s %s\n" "$port" "${RESULTS["$port"]:-SKIPPED}"
done

exit "$FAILED"
