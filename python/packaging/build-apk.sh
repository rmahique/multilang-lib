#!/usr/bin/env bash
# Build an .apk for Alpine Linux, via abuild.
#
# Must run inside (or targeting) an actual Alpine container -- abuild
# itself, its dependency resolution, and Alpine's own package versioning
# rules are all Alpine-specific. Must also run as root (matching every
# other build-*.sh in this repo, and how build-packages.yml's `docker
# run` invokes them) even though abuild itself refuses to run its build/
# package steps as root -- see packaging/README.md's "Alpine" section:
# this script stages the source as root, chowns it to the `builder` user
# packaging/docker/Dockerfile.alpine creates, then `su`s to that user
# just for the `abuild -r` step.
set -euo pipefail

cd "$(dirname "$0")/.."   # python/

PYPROJECT_VERSION="$(grep -m1 '^version *= *"' pyproject.toml | sed -E 's/^version *= *"([^"]+)".*/\1/')"
VERSION="$(../scripts/compute-version.sh apk "$PYPROJECT_VERSION")"
if [ -z "$VERSION" ]; then
    echo "error: could not determine version" >&2
    exit 1
fi

BUILD_ROOT="/home/builder/apk-build"
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"
cp -r multilang tests pyproject.toml README.md LICENSE packaging/apk/APKBUILD "$BUILD_ROOT/"
chown -R builder:abuild "$BUILD_ROOT"

# Dockerfile.alpine installs its baked-in deps with `apk add --no-cache`,
# which deliberately leaves no package index behind -- fine as long as
# every depends=/makedepends= this APKBUILD declares happens to already
# be installed, but `apk update` here makes that not a fragile
# assumption: without a real index, `abuild -r`'s own dependency
# resolution fails with "no such package" the moment anything isn't
# already pre-baked into the image, even for a package that genuinely
# exists in the repo.
apk update

# MULTILANG_VERSION: read by APKBUILD's pkgver=${MULTILANG_VERSION:-0.1.0}
# -- abuild has no rpmbuild-style --define flag to override a variable
# the spec/APKBUILD already assigns, so the APKBUILD itself falls back to
# an environment variable instead (same idea as the RPM spec's
# %{!?version: %global version 0.1.0} fallback, just spelled the POSIX
# shell way since APKBUILD *is* a shell script).
su builder -c "cd '$BUILD_ROOT' && export MULTILANG_VERSION='$VERSION' && abuild -r"

# sha512sum next to each package this run produced, not a combined
# SHA512SUMS -- same reasoning as build-deb.sh/build-rpm.sh. Left under
# ~builder/packages/ (abuild's own REPODEST-based output tree) rather
# than copied anywhere else here: build-packages.yml's `collect:` step
# (a recursive `find ... -name "*.apk" -o -name "*.sha512"`, same defensive
# pattern already used for RPM's nested RPMS/SRPMS output) does the actual
# collecting into dist-packages/.
find /home/builder/packages -name "*.apk" | while read -r pkg; do
    sha512sum "$pkg" > "${pkg}.sha512"
done

echo "Build artifacts placed under ~builder/packages/ (plus matching .sha512 files)."
