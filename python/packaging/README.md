# Packaging

Native packages for the three major Linux packaging families. These build
scripts must be run **inside a container/chroot for the actual target
distro** — package macros, dependency names, and Python versions are
distro-specific, so building on an unrelated host is not representative.

| Family | Script | Spec/control files |
|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/multilang.spec` |
| openSUSE, SLES | `build-rpm.sh` | `rpm/multilang.spec` (same spec, `%if 0%{?suse_version}` branches handle the differences) |

## Debian / Ubuntu

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src debian:bookworm bash -c '
  apt-get update &&
  apt-get install -y build-essential debhelper dh-python python3-all \
                      python3-setuptools python3-pytest python3-build \
                      pybuild-plugin-pyproject &&
  packaging/build-deb.sh
'
```

`pybuild-plugin-pyproject` is required because this project builds via
`pyproject.toml` (PEP 517/setuptools), not a legacy `setup.py` — without
it, `dh_auto_configure` fails with "PEP517 plugin dependencies are not
available."

Resulting `.deb` files land in the parent directory of `python/`.

## RHEL / CentOS Stream / Fedora

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src fedora:latest bash -c '
  dnf install -y rpm-build python3-devel python3-setuptools python3-pytest python3-pip &&
  pip install build &&
  packaging/build-rpm.sh
'
```

## openSUSE / SLES

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src opensuse/leap:15 bash -c '
  zypper --non-interactive install rpm-build python310 python310-devel python310-setuptools &&
  packaging/build-rpm.sh
'
```

Leap 15's default `python3` is 3.6 (too old for this project); the spec
builds against the versioned `python310` package instead, using pip
bootstrapped offline via `ensurepip` — no `python3-pip`/`pip install
build` needed (neither `python310-pip` nor a `build` package for
python310 exists in Leap 15's repos). `%check` runs the test suite if
pytest happens to be importable and skips it otherwise, since
`python310-pytest` isn't packaged either and a real build farm (mock/OBS)
wouldn't have network access to `pip install` it anyway.

Resulting RPMs land under `~/rpmbuild/RPMS/noarch/` inside the container
(bind-mount `$HOME` or copy them out before the container exits).

## Before a real release

- For a signed/repo-distributed build, use `mock` (RPM) or `sbuild`/`pbuilder`
  (Debian) instead of a bare `docker run`, and go through the project's
  normal OBS/COPR/PPA signing flow rather than `rpmbuild`/`dpkg-buildpackage`
  directly.
