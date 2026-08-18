#!/usr/bin/env bash
# Build an .apk for Alpine Linux, via abuild. See python/packaging/
# build-apk.sh for the full rationale (staging as root, building as the
# unprivileged `builder` user Dockerfile.alpine creates) -- this script
# follows the identical pattern, just staging the PHP source files.
set -euo pipefail

cd "$(dirname "$0")/.."   # php/

VERSION="$(../scripts/compute-version.sh apk 0.1.0)"
if [ -z "$VERSION" ]; then
    echo "error: could not determine version" >&2
    exit 1
fi

BUILD_ROOT="/home/builder/apk-build"
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"
cp -r src composer.json ../LICENSE packaging/apk/APKBUILD "$BUILD_ROOT/"
chown -R builder:abuild "$BUILD_ROOT"

su builder -c "cd '$BUILD_ROOT' && export MULTILANG_VERSION='$VERSION' && abuild -r"

find /home/builder/packages -name "*.apk" | while read -r pkg; do
    sha512sum "$pkg" > "${pkg}.sha512"
done

echo "Build artifacts placed under ~builder/packages/ (plus matching .sha512 files)."
