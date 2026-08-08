#!/usr/bin/env bash
# Build a .deb for Debian/Ubuntu and derivatives -- ships CommonJS source
# only (no vendored node_modules/), wrapping this project's npm package.
# See packaging/README.md.
set -euo pipefail

cd "$(dirname "$0")/.."   # javascript/

PKG=node-multilang
# Avoid requiring a `node` binary just to read one field -- this build
# doesn't otherwise need a JS runtime, only debhelper.
PACKAGE_JSON_VERSION="$(grep -m1 '"version"' package.json | sed -E 's/.*"version" *: *"([^"]+)".*/\1/')"
VERSION="$(../scripts/compute-version.sh deb "${PACKAGE_JSON_VERSION:-0.1.0}")"

ORIG_TARBALL="../${PKG}_${VERSION}.orig.tar.gz"
tar czf "$ORIG_TARBALL" src package.json README.md -C .. LICENSE

rm -rf debian
cp -r packaging/debian debian

{
    echo "${PKG} (${VERSION}-1) unstable; urgency=medium"
    echo
    echo "  * Automated build for commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)."
    echo
    echo " -- Raúl Mahiques <claude.ia@raulmahiques.com>  $(date -u -R)"
    echo
    cat debian/changelog
} > debian/changelog.new
mv debian/changelog.new debian/changelog

dpkg-buildpackage -us -uc -b

rm -rf debian

for pkg in ../*_"${VERSION}"*.deb; do
    [ -e "$pkg" ] || continue
    sha512sum "$pkg" > "${pkg}.sha512"
done

echo "Build artifacts placed in the parent directory (../*.deb, ../*.deb.sha512)."
