# Packaging (Go)

Ships Go **source only**, no compiled binary — Go isn't normally distro-
packaged as a library the way C or Python are; consumers `go build`/`go get`
against source. These `.deb`/`.rpm` packages install that source under the
GOPATH-style path Debian's Go packaging policy uses
(`/usr/share/gocode/src/github.com/rmahique/multilang-lib/go/`), so it's
usable as a system-wide `GOPATH` dependency without needing network access
to `go get` it.

Must run inside (or targeting) the actual distro you're packaging for, same
caveat as every other language's packaging in this repo.

| Family | Script | Spec/control files |
|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/golang-github-rmahique-multilang-lib.spec` |
| openSUSE Leap, Tumbleweed, SLES | `build-rpm.sh` | same spec — no distro branching needed, this package has no compiled artifact or Python-style version quirks |

## Debian / Ubuntu

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src debian:bookworm bash -c '
  apt-get update &&
  apt-get install -y build-essential debhelper git &&
  packaging/build-deb.sh
'
```

## RHEL / CentOS Stream / Fedora

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src fedora:latest bash -c '
  dnf install -y rpm-build git &&
  packaging/build-rpm.sh
'
```

## openSUSE (Leap 15, Tumbleweed) / SLES

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src opensuse/tumbleweed bash -c '
  zypper --non-interactive install rpm-build git &&
  packaging/build-rpm.sh
'
```

(swap the image for `opensuse/leap:15` — the spec is identical either way).

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and this
project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly.
