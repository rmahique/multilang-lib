#!/usr/bin/env bash
# Build an RPM for RHEL/CentOS/Fedora or openSUSE/SLES.
#
# Must run inside (or targeting) the actual distro you're packaging for —
# a mock chroot, an osc build, or a matching container — not on an
# unrelated host OS, since macro names (python3_sitelib, py3_build) and
# dependency package names differ per distro family.
set -euo pipefail

cd "$(dirname "$0")/.."   # python/

# Not `python3 -c "import tomllib"`: tomllib needs Python 3.11+, and this
# script has to run on whatever python3 the target distro's container
# ships by default (e.g. openSUSE Leap 15's is 3.10). The version line in
# pyproject.toml is a single simple `version = "x.y.z"` string, so a
# portable grep/sed avoids needing any TOML parser at all.
VERSION=$(grep -m1 '^version *= *"' pyproject.toml | sed -E 's/^version *= *"([^"]+)".*/\1/')
if [ -z "$VERSION" ]; then
    echo "error: could not read version from pyproject.toml" >&2
    exit 1
fi

# A plain tarball, not `python3 -m build --sdist`: RPM's %prep/%autosetup
# just needs a `multilang-${VERSION}/` directory inside a .tar.gz, not a
# proper PEP 517 sdist -- and this avoids needing the `build` PyPI module
# (and thus network access, and thus any particular python3 version) at
# all. Every distro family's rpmbuild gets exactly the same tarball this
# way.
mkdir -p dist
STAGING="$(mktemp -d)"
DESTDIR="${STAGING}/multilang-${VERSION}"
mkdir -p "$DESTDIR"
cp -r multilang tests pyproject.toml README.md LICENSE "$DESTDIR/"
tar czf "dist/multilang-${VERSION}.tar.gz" -C "$STAGING" "multilang-${VERSION}"
rm -rf "$STAGING"

RPMBUILD_ROOT="${HOME}/rpmbuild"
mkdir -p "${RPMBUILD_ROOT}"/{SOURCES,SPECS}
cp "dist/multilang-${VERSION}.tar.gz" "${RPMBUILD_ROOT}/SOURCES/"
cp packaging/rpm/multilang.spec "${RPMBUILD_ROOT}/SPECS/"

# --define "_topdir ...": openSUSE/SLES's rpmbuild defaults %_topdir to
# /usr/src/packages instead of ~/rpmbuild (Fedora/RHEL/Debian's default),
# so without this override the build looks for SOURCES/SPECS in the wrong
# place on SUSE. Harmless to set explicitly everywhere else too, since it
# matches what those distros already default to.
rpmbuild --define "_topdir ${RPMBUILD_ROOT}" -ba "${RPMBUILD_ROOT}/SPECS/multilang.spec"
echo "Build artifacts placed under ${RPMBUILD_ROOT}/RPMS and .../SRPMS."
