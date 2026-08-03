#!/usr/bin/env bash
#
# Runs the shared conformance suite (cases.json) against real PostgreSQL
# and MySQL servers, for every language port. Everything — the database
# and each language's test run — happens inside containers on a shared
# container network; nothing here invokes a host-installed language
# toolchain. This makes the result reproducible regardless of what's
# installed on the machine running this script, and is exactly what CI
# runners do.
#
# Every version this script depends on is a variable, never hardcoded:
#
#   POSTGRES_VERSION     default: 16
#   MYSQL_VERSION         default: 8.0
#   PYTHON_VERSION        default: 3.11   (Dockerfile build ARG)
#   NODE_VERSION           default: 20     (Dockerfile build ARG)
#   PHP_VERSION             default: 8.2    (Dockerfile build ARG)
#   GO_VERSION               default: 1.23   (Dockerfile build ARG)
#   DEBIAN_VERSION           default: bookworm (Dockerfile build ARG, C/C++ image)
#
# Container engine is auto-detected (docker preferred, podman as
# fallback) or forced with:
#
#   CONTAINER_ENGINE   docker | podman
#
# Usage:
#   ./run-live-db-tests.sh                    # all ports, both backends
#   ./run-live-db-tests.sh python go           # only these ports
#   POSTGRES_VERSION=15 MYSQL_VERSION=5.7 ./run-live-db-tests.sh
#   PYTHON_VERSION=3.12 ./run-live-db-tests.sh python
#
# One container per language (not per language/backend pair): each
# container runs the suite against postgres, then mysql, internally (see
# conformance/docker/run-both-backends.sh). Exit code is non-zero if any
# language/backend combination fails. Containers and the network are
# always torn down on exit, including on failure or Ctrl-C.

set -u
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

POSTGRES_VERSION="${POSTGRES_VERSION:-16}"
MYSQL_VERSION="${MYSQL_VERSION:-8.0}"
PYTHON_VERSION="${PYTHON_VERSION:-3.11}"
NODE_VERSION="${NODE_VERSION:-20}"
PHP_VERSION="${PHP_VERSION:-8.2}"
GO_VERSION="${GO_VERSION:-1.23}"
DEBIAN_VERSION="${DEBIAN_VERSION:-bookworm}"

DB_USER="multilang"
DB_PASSWORD="multilang"
DB_NAME="multilang"
MYSQL_ROOT_PASSWORD="multilang_root"

NETWORK="multilang-conformance-net"
PG_CONTAINER="multilang-conformance-postgres"
MYSQL_CONTAINER="multilang-conformance-mysql"

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

# --- cleanup -----------------------------------------------------------------

cleanup() {
    echo "Tearing down containers and network..."
    "$CONTAINER_ENGINE" rm -f "$PG_CONTAINER" "$MYSQL_CONTAINER" >/dev/null 2>&1 || true
    "$CONTAINER_ENGINE" network rm "$NETWORK" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

cleanup # in case a previous run left things behind
"$CONTAINER_ENGINE" network create "$NETWORK" >/dev/null

# --- start database containers (network-internal only, no host port publish) ---

echo "Starting postgres:${POSTGRES_VERSION}..."
"$CONTAINER_ENGINE" run -d --name "$PG_CONTAINER" --network "$NETWORK" \
    -e POSTGRES_USER="$DB_USER" \
    -e POSTGRES_PASSWORD="$DB_PASSWORD" \
    -e POSTGRES_DB="$DB_NAME" \
    "postgres:${POSTGRES_VERSION}" >/dev/null

echo "Starting mysql:${MYSQL_VERSION}..."
"$CONTAINER_ENGINE" run -d --name "$MYSQL_CONTAINER" --network "$NETWORK" \
    -e MYSQL_USER="$DB_USER" \
    -e MYSQL_PASSWORD="$DB_PASSWORD" \
    -e MYSQL_DATABASE="$DB_NAME" \
    -e MYSQL_ROOT_PASSWORD="$MYSQL_ROOT_PASSWORD" \
    "mysql:${MYSQL_VERSION}" >/dev/null

# --- wait for readiness --------------------------------------------------------

wait_for() {
    local name="$1" check_cmd="$2" timeout_s="$3"
    local waited=0
    echo -n "Waiting for ${name} to be ready"
    until "$CONTAINER_ENGINE" exec "$name" sh -c "$check_cmd" >/dev/null 2>&1; do
        if [ "$waited" -ge "$timeout_s" ]; then
            echo " TIMED OUT after ${timeout_s}s"
            "$CONTAINER_ENGINE" logs "$name" 2>&1 | tail -40
            return 1
        fi
        echo -n "."
        sleep 2
        waited=$((waited + 2))
    done
    echo " ready"
}

wait_for "$PG_CONTAINER" "pg_isready -U ${DB_USER}" 90 || exit 1
wait_for "$MYSQL_CONTAINER" "mysqladmin ping -h localhost -u root -p${MYSQL_ROOT_PASSWORD}" 120 || exit 1

# Resolve each database container's IP address on the shared network
# directly, rather than relying on container-name DNS resolution (not
# guaranteed to be enabled for every engine/network-backend combination,
# and unnecessary complexity for what's just two fixed containers).
ip_of() {
    # `index` rather than dot-notation: the network name contains
    # hyphens, which Go's text/template field selector syntax can't
    # address directly (`.Networks.multilang-conformance-net` is invalid).
    "$CONTAINER_ENGINE" inspect -f "{{(index .NetworkSettings.Networks \"${NETWORK}\").IPAddress}}" "$1"
}
PG_IP="$(ip_of "$PG_CONTAINER")"
MYSQL_IP="$(ip_of "$MYSQL_CONTAINER")"
echo "postgres IP: ${PG_IP}, mysql IP: ${MYSQL_IP}"

# --- per-port image build/run ---------------------------------------------------
# Each language's test image is built once and run once — a single
# container per language, not one per (language, backend) pair. Inside
# that one container, the entrypoint (run-both-backends.sh) runs the
# suite against postgres, then against mysql, on the same network as the
# database containers, addressed by IP (never localhost, never a
# host-published port).

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

run_port() {
    local port="$1"
    "$CONTAINER_ENGINE" run --rm --network "$NETWORK" \
        -e MULTILANG_PG_HOST="$PG_IP" \
        -e MULTILANG_PG_PORT=5432 \
        -e MULTILANG_MYSQL_HOST="$MYSQL_IP" \
        -e MULTILANG_MYSQL_PORT=3306 \
        -e MULTILANG_DB_USER="$DB_USER" \
        -e MULTILANG_DB_PASSWORD="$DB_PASSWORD" \
        -e MULTILANG_DB_NAME="$DB_NAME" \
        "multilang-conformance-${port}"
}

# --- run everything --------------------------------------------------------------

declare -A RESULTS
FAILED=0

for port in "${PORTS[@]}"; do
    echo ""
    echo "=== ${port} (postgres + mysql) ==="
    if run_port "$port"; then
        RESULTS["$port"]="PASS"
    else
        RESULTS["$port"]="FAIL"
        FAILED=1
    fi
done

# --- summary -----------------------------------------------------------------

echo ""
echo "=== Summary (postgres:${POSTGRES_VERSION}, mysql:${MYSQL_VERSION}) ==="
for port in "${PORTS[@]}"; do
    printf "%-12s %s\n" "$port" "${RESULTS["$port"]:-SKIPPED}"
done

exit "$FAILED"
