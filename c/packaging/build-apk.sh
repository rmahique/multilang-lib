#!/usr/bin/env bash
# Build .apks for Alpine Linux -- libmultilang (runtime .so) +
# libmultilang-dev (headers + unversioned .so symlinks, split
# automatically by abuild's default_dev(), see packaging/apk/APKBUILD),
# actually compiled via ../Makefile's `make install`. See python/
# packaging/build-apk.sh for the staging-as-root/build-as-`builder`
# rationale this script also follows.
#
# Must run inside (or targeting) the actual distro you're packaging for:
# abuild's own automatic shared-library dependency tracking needs the
# real libpq/sqlite3/libssl/mariadb-connector-c runtime packages
# installed to resolve libmultilang's dependencies correctly.
set -euo pipefail

cd "$(dirname "$0")/.."   # c/

VERSION="$(../scripts/compute-version.sh apk 0.1.0)"
if [ -z "$VERSION" ]; then
    echo "error: could not determine version" >&2
    exit 1
fi

BUILD_ROOT="/home/builder/apk-build"
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"
cp -r Makefile src include README.md ../LICENSE packaging/apk/APKBUILD "$BUILD_ROOT/"
chown -R builder:abuild "$BUILD_ROOT"

# Dockerfile.alpine installs its baked-in deps with `apk add --no-cache`,
# which deliberately leaves no package index behind -- fine for those
# packages (already installed), but `abuild -r`'s own dependency
# resolution needs a real index to search even to double check an
# already-satisfied depends/makedepends, or it fails with "no such
# package". `apk update` fetches that index once, before handing off to
# `abuild`.
apk update

su builder -c "cd '$BUILD_ROOT' && export MULTILANG_VERSION='$VERSION' && abuild -r"

find /home/builder/packages -name "*.apk" | while read -r pkg; do
    sha512sum "$pkg" > "${pkg}.sha512"
done

echo "Build artifacts placed under ~builder/packages/ (plus matching .sha512 files)."
