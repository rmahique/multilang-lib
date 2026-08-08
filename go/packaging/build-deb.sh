#!/usr/bin/env bash
# Build a .deb for Debian/Ubuntu and derivatives -- ships Go source only
# (no compiled binary), per Debian's Go packaging convention. See
# packaging/README.md.
#
# Must run inside (or targeting) the actual distro you're packaging for --
# a sbuild/pbuilder chroot or a debian/ubuntu container.
set -euo pipefail

cd "$(dirname "$0")/.."   # go/

PKG=golang-github-rmahique-multilang-lib

# This package has no version of its own beyond the repo's -- fall back
# to 0.1.0 like the other ports do when no tag exists yet.
VERSION="$(../scripts/compute-version.sh deb 0.1.0)"

# debian/source format 3.0 (quilt) requires a <pkg>_<version>.orig.tar.gz
# in the parent directory. Pull LICENSE from the repo root (go/ doesn't
# keep its own copy, unlike python/) via a second tar -C.
ORIG_TARBALL="../${PKG}_${VERSION}.orig.tar.gz"
tar czf "$ORIG_TARBALL" ./*.go go.mod go.sum README.md -C .. LICENSE

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
