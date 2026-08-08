#!/usr/bin/env bash
# Build .debs for Debian/Ubuntu and derivatives -- libmultilang0 (runtime
# .so) + libmultilang-dev (headers), actually compiled via ../Makefile's
# `make install`. See packaging/README.md.
#
# Must run inside (or targeting) the actual distro you're packaging for:
# dpkg-shlibdeps needs the real libpq/libsqlite3/libssl/libmariadb
# runtime packages installed to resolve libmultilang0's ${shlibs:Depends}
# correctly for that distro.
set -euo pipefail

cd "$(dirname "$0")/.."   # c/

PKG=multilang
VERSION="$(../scripts/compute-version.sh deb 0.1.0)"

ORIG_TARBALL="../${PKG}_${VERSION}.orig.tar.gz"
tar czf "$ORIG_TARBALL" Makefile src include README.md -C .. LICENSE

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
