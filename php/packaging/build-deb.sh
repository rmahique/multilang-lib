#!/usr/bin/env bash
# Build a .deb for Debian/Ubuntu and derivatives -- ships PSR-4 PHP
# source only (no vendor/), wrapping this project's Composer package.
# See packaging/README.md.
set -euo pipefail

cd "$(dirname "$0")/.."   # php/

PKG=php-multilang
# Avoid requiring a `php` (or `jq`) binary just to read one field.
COMPOSER_VERSION="$(grep -m1 '"version"' composer.json 2>/dev/null | sed -E 's/.*"version" *: *"([^"]+)".*/\1/' || true)"
VERSION="$(../scripts/compute-version.sh deb "${COMPOSER_VERSION:-0.1.0}")"

ORIG_TARBALL="../${PKG}_${VERSION}.orig.tar.gz"
tar czf "$ORIG_TARBALL" src composer.json README.md -C .. LICENSE

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
