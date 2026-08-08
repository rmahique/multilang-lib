#!/usr/bin/env bash
# Build an RPM for RHEL/CentOS/Fedora or openSUSE Leap/Tumbleweed/SLES --
# ships PSR-4 PHP source only (no vendor/). Identical across every RPM
# family, no distro-specific flag needed.
set -euo pipefail

cd "$(dirname "$0")/.."   # php/

PKG=multilang
COMPOSER_VERSION="$(grep -m1 '"version"' composer.json 2>/dev/null | sed -E 's/.*"version" *: *"([^"]+)".*/\1/')"
VERSION="$(../scripts/compute-version.sh rpm "${COMPOSER_VERSION:-0.1.0}")"
if [ -z "$VERSION" ]; then
    echo "error: could not determine version" >&2
    exit 1
fi

mkdir -p dist
STAGING="$(mktemp -d)"
DESTDIR="${STAGING}/${PKG}-${VERSION}"
mkdir -p "$DESTDIR"
cp -r src composer.json README.md ../LICENSE "$DESTDIR/"
tar czf "dist/${PKG}-${VERSION}.tar.gz" -C "$STAGING" "${PKG}-${VERSION}"
rm -rf "$STAGING"

RPMBUILD_ROOT="${HOME}/rpmbuild"
mkdir -p "${RPMBUILD_ROOT}"/{SOURCES,SPECS}
cp "dist/${PKG}-${VERSION}.tar.gz" "${RPMBUILD_ROOT}/SOURCES/"
cp packaging/rpm/php-multilang.spec "${RPMBUILD_ROOT}/SPECS/"

rpmbuild --define "_topdir ${RPMBUILD_ROOT}" --define "version ${VERSION}" \
    -ba "${RPMBUILD_ROOT}/SPECS/php-multilang.spec"

find "${RPMBUILD_ROOT}/RPMS" "${RPMBUILD_ROOT}/SRPMS" -name "*${VERSION}*.rpm" 2>/dev/null | while read -r pkg; do
    sha512sum "$pkg" > "${pkg}.sha512"
done

echo "Build artifacts placed under ${RPMBUILD_ROOT}/RPMS and .../SRPMS (plus matching .sha512 files)."
