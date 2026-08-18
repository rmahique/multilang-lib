# Packaging

Native packages for the four major Linux packaging families. Each build
must run **inside a container for the actual target distro** — package
macros, dependency names, and Python versions are distro-specific, so
building on an unrelated host is not representative. `docker/` has a
Dockerfile per distro with every build dependency baked in (this is what
`.github/workflows/build-packages.yml` builds and runs in CI — same
image, same commands, locally or in CI).

| Family | Script | Spec/control files | Dockerfile |
|---|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` | `docker/Dockerfile.debian-bookworm` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/multilang.spec` | `docker/Dockerfile.fedora-latest` |
| openSUSE Leap 15 | `build-rpm.sh` | `rpm/multilang.spec` (same spec, `%if 0%{?suse_version}` branches handle the differences; set `MULTILANG_LEAP15_PYTHON_WORKAROUND=1` to build against the versioned `python310` package) | `docker/Dockerfile.opensuse-leap-15` |
| openSUSE Tumbleweed | `build-rpm.sh` | `rpm/multilang.spec` (same spec/mechanism as Leap, but builds against the distro's current default `python3` -- do **not** set `MULTILANG_LEAP15_PYTHON_WORKAROUND`) | `docker/Dockerfile.opensuse-tumbleweed` |
| SLES 16 | `build-rpm.sh` | `rpm/multilang.spec` (same spec/mechanism as Tumbleweed -- SLES 16's default `python3` is already current, so do **not** set `MULTILANG_LEAP15_PYTHON_WORKAROUND` here either) | `docker/Dockerfile.sles-16` |
| Alpine | `build-apk.sh` | `apk/APKBUILD` (a completely different format/toolchain, `abuild` -- see the Alpine section below) | `docker/Dockerfile.alpine` |

## Debian / Ubuntu

```bash
docker build -t multilang-python-deb -f packaging/docker/Dockerfile.debian-bookworm packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/python multilang-python-deb packaging/build-deb.sh
```

`pybuild-plugin-pyproject` (in the Dockerfile) is required because this
project builds via `pyproject.toml` (PEP 517/setuptools), not a legacy
`setup.py` — without it, `dh_auto_configure` fails with "PEP517 plugin
dependencies are not available."

Resulting `.deb` files land in the parent directory of `python/`.

## RHEL / CentOS Stream / Fedora

```bash
docker build -t multilang-python-rpm -f packaging/docker/Dockerfile.fedora-latest packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/python multilang-python-rpm packaging/build-rpm.sh
```

## openSUSE Leap / SLES

```bash
docker build -t multilang-python-leap -f packaging/docker/Dockerfile.opensuse-leap-15 packaging/docker
docker run --rm -e MULTILANG_LEAP15_PYTHON_WORKAROUND=1 \
  -v "$(pwd)/..":/workspace -w /workspace/python multilang-python-leap packaging/build-rpm.sh
```

Leap 15's default `python3` is 3.6 (too old for this project); the spec
builds against the versioned `python310` package instead, using pip
bootstrapped offline via `ensurepip` — no `python3-pip`/`pip install
build` needed (neither `python310-pip` nor a `build` package for
python310 exists in Leap 15's repos). `%check` runs the test suite if
pytest happens to be importable and skips it otherwise, since
`python310-pytest` isn't packaged either and a real build farm (mock/OBS)
wouldn't have network access to `pip install` it anyway.

## openSUSE Tumbleweed

```bash
docker build -t multilang-python-tumbleweed -f packaging/docker/Dockerfile.opensuse-tumbleweed packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/python multilang-python-tumbleweed packaging/build-rpm.sh
```

Same spec and pip-wheel build mechanism as Leap 15, but Tumbleweed is a
rolling release with an already-current default `python3`, so it builds
against that directly instead of a versioned `python310` package — do
**not** set `MULTILANG_LEAP15_PYTHON_WORKAROUND` here. `%check` runs for
real on Tumbleweed since `python3-pytest` is expected to be installable.

Resulting RPMs (all three SUSE flavors) land under
`~/rpmbuild/RPMS/noarch/` *inside* the container, so `build-rpm.sh` alone
(without copying them out before the container exits) isn't enough to
get them onto the host — see how `build-packages.yml`'s `collect:` step
chains a `find ... | xargs cp` into the same `docker run` invocation.

## SLES 16

```bash
docker build -t multilang-python-sles -f packaging/docker/Dockerfile.sles-16 packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/python multilang-python-sles packaging/build-rpm.sh
```

Same spec and pip-wheel build mechanism as Tumbleweed: SLES 16 ships a
current default `python3` (3.13), so it builds against that directly —
do **not** set `MULTILANG_LEAP15_PYTHON_WORKAROUND` here either.
`docker/Dockerfile.sles-16` is `FROM registry.suse.com/bci/bci-base:16.0`
— SUSE's free, anonymously-pullable image for SLE 16, pre-configured
with the `SLE_BCI` repo so `zypper install` works without an SCC
registration/subscription. `%check` runs for real (`python3-pytest` is
expected to be installable from `SLE_BCI`, same as Tumbleweed).

## Alpine

```bash
docker build -t multilang-python-alpine -f packaging/docker/Dockerfile.alpine packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/python multilang-python-alpine packaging/build-apk.sh
```

Alpine's `abuild`/APKBUILD is a genuinely different toolchain from
`.deb`/`.rpm`, not just a fourth distro on the existing rpmbuild path,
with two things worth knowing before touching `apk/APKBUILD` or
`build-apk.sh`:

- **abuild refuses to run its build/package steps as root.** Every other
  Dockerfile in this repo just `apt`/`dnf`/`zypper install`s and lets the
  rest of the build run as whatever user `docker run` defaults to (root).
  `docker/Dockerfile.alpine` instead creates a `builder` user in the
  `abuild` group with passwordless sudo, and generates an ephemeral,
  throwaway signing key for it (`abuild-keygen -a -i -n`) at image-build
  time — self-signed and CI-only, not meant for a real distributed repo
  (see "Before a real release" below). `build-apk.sh` stages the source
  as root (so it can read the bind-mounted checkout), `chown`s that
  staging copy to `builder`, then `su builder -c 'abuild -r'` for the
  actual build/package step.
- **`pkgver` can't be overridden from the command line** the way
  `rpmbuild --define "version ..."` overrides the RPM spec's `Version:`.
  `apk/APKBUILD` instead reads it from an environment variable
  (`pkgver=${MULTILANG_VERSION:-0.1.0}`), which `build-apk.sh` exports
  before invoking `abuild`.

Resulting `.apk` files land under `~builder/packages/` (abuild's own
`REPODEST`-based output tree) *inside* the container — same
"collect before the container exits" caveat as the RPM builds above, see
`build-packages.yml`'s `collect:` step.

`python3-psycopg2`/`python3-pymysql`-equivalent optional backend support
ships as `py3-multilang-postgres`/`py3-multilang-mysql` subpackages,
mirroring the RPM spec's `-postgres`/`-mysql` split.

## Before a real release

- For a signed/repo-distributed build, use `mock` (RPM) or `sbuild`/`pbuilder`
  (Debian) instead of a bare `docker run`, and go through the project's
  normal OBS/COPR/PPA signing flow rather than `rpmbuild`/`dpkg-buildpackage`
  directly.
- The Alpine build's signing key is ephemeral and regenerated on every
  image build (`docker/Dockerfile.alpine`'s `abuild-keygen -a -i -n`) —
  fine for a CI artifact nobody else's `apk` trusts, but a real,
  installable-by-others Alpine repo needs a real persistent key and
  Alpine's own aports/CI signing infrastructure instead.
