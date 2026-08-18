# Packaging

Native packages for the three major Linux packaging families. Each build
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

## Before a real release

- For a signed/repo-distributed build, use `mock` (RPM) or `sbuild`/`pbuilder`
  (Debian) instead of a bare `docker run`, and go through the project's
  normal OBS/COPR/PPA signing flow rather than `rpmbuild`/`dpkg-buildpackage`
  directly.
