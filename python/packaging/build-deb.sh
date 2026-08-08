#!/usr/bin/env bash
# Build a .deb for Debian/Ubuntu and derivatives.
#
# Must run inside (or targeting) the actual distro you're packaging for —
# a sbuild/pbuilder chroot or a debian/ubuntu container — not on an
# unrelated host OS, since dependency names and Python versions differ
# per release.
set -euo pipefail

cd "$(dirname "$0")/.."   # python/

# Don't assume pip is present or writable: the documented container setup
# (see packaging/README.md) already installs the `build` module via the
# distro's own package manager (python3-build on Debian/Fedora/openSUSE).
# Only fall back to pip if `build` genuinely isn't available, and even
# then only as a last resort — pip may not exist, or may be blocked by
# PEP 668's externally-managed-environment guard on modern distros.
if ! python3 -c "import build" >/dev/null 2>&1; then
    echo "python3 'build' module not found; attempting pip install as a fallback..." >&2
    python3 -m pip install --quiet build || {
        echo "error: 'build' module missing and pip install failed." >&2
        echo "Install it via your distro's package manager first (e.g. python3-build)." >&2
        exit 1
    }
fi
# --no-isolation: the container setup (packaging/README.md) already
# installs build/setuptools system-wide via the distro's package manager;
# `build`'s default isolated-venv mode would otherwise need python3-venv,
# which isn't part of that minimal dependency set.
python3 -m build --sdist --no-isolation

rm -rf debian
cp -r packaging/debian debian

# Every commit gets a package: compute-version.sh returns the exact tag
# when HEAD is a release, otherwise the latest tag (or pyproject.toml's
# version if no tag exists yet) plus today's date. Prepend a fresh
# changelog stanza with that version rather than requiring `dch`
# (devscripts) just to edit one line -- the packaging/README.md container
# setups don't install devscripts, and this is the only field that needs
# to change per build.
PYPROJECT_VERSION="$(grep -m1 '^version *= *"' pyproject.toml | sed -E 's/^version *= *"([^"]+)".*/\1/')"
VERSION="$(../scripts/compute-version.sh deb "$PYPROJECT_VERSION")"
{
    echo "multilang (${VERSION}-1) unstable; urgency=medium"
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

# sha512sum next to each package this run produced, not a combined
# SHA512SUMS -- callers (CI artifact upload, a release page) may only
# want one specific package's checksum, not the whole batch's.
for pkg in ../*_"${VERSION}"*.deb; do
    [ -e "$pkg" ] || continue
    sha512sum "$pkg" > "${pkg}.sha512"
done

echo "Build artifacts placed in the parent directory (../*.deb, ../*.deb.sha512)."
