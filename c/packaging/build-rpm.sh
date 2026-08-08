#!/usr/bin/env bash
# Build RPMs for RHEL/CentOS/Fedora or openSUSE Leap/Tumbleweed/SLES --
# libmultilang0-equivalent runtime + -devel, compiled via ../Makefile's
# `make install`. One spec, no distro branching needed (see the spec's
# own comment on pkgconfig(X) BuildRequires).
set -euo pipefail

cd "$(dirname "$0")/.."   # c/

PKG=multilang
VERSION="$(../scripts/compute-version.sh rpm 0.1.0)"
if [ -z "$VERSION" ]; then
    echo "error: could not determine version" >&2
    exit 1
fi

mkdir -p dist
STAGING="$(mktemp -d)"
DESTDIR="${STAGING}/${PKG}-${VERSION}"
mkdir -p "$DESTDIR"
cp -r Makefile src include README.md ../LICENSE "$DESTDIR/"
tar czf "dist/${PKG}-${VERSION}.tar.gz" -C "$STAGING" "${PKG}-${VERSION}"
rm -rf "$STAGING"

RPMBUILD_ROOT="${HOME}/rpmbuild"
mkdir -p "${RPMBUILD_ROOT}"/{SOURCES,SPECS}
cp "dist/${PKG}-${VERSION}.tar.gz" "${RPMBUILD_ROOT}/SOURCES/"
cp packaging/rpm/multilang.spec "${RPMBUILD_ROOT}/SPECS/"

rpmbuild --define "_topdir ${RPMBUILD_ROOT}" --define "version ${VERSION}" \
    -ba "${RPMBUILD_ROOT}/SPECS/multilang.spec"

find "${RPMBUILD_ROOT}/RPMS" "${RPMBUILD_ROOT}/SRPMS" -name "*${VERSION}*.rpm" 2>/dev/null | while read -r pkg; do
    sha512sum "$pkg" > "${pkg}.sha512"
done

echo "Build artifacts placed under ${RPMBUILD_ROOT}/RPMS and .../SRPMS (plus matching .sha512 files)."
