# Packaging (Go)

Ships Go **source only**, no compiled binary — Go isn't normally distro-
packaged as a library the way C or Python are; consumers `go build`/`go get`
against source. These `.deb`/`.rpm` packages install that source under the
GOPATH-style path Debian's Go packaging policy uses
(`/usr/share/gocode/src/github.com/rmahique/multilang-lib/go/`), so it's
usable as a system-wide `GOPATH` dependency without needing network access
to `go get` it.

Must build inside a container for the actual distro you're packaging for,
same caveat as every other language's packaging in this repo. `docker/`
has a Dockerfile per distro with every build dependency baked in (this
is what `.github/workflows/build-packages.yml` builds and runs in CI —
same image, same commands, locally or in CI).

| Family | Script | Spec/control files | Dockerfile |
|---|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` | `docker/Dockerfile.debian-bookworm` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/golang-github-rmahique-multilang-lib.spec` | `docker/Dockerfile.fedora-latest` |
| openSUSE Leap 15 | `build-rpm.sh` | same spec — no distro branching needed, this package has no compiled artifact or Python-style version quirks | `docker/Dockerfile.opensuse-leap-15` |
| openSUSE Tumbleweed | `build-rpm.sh` | same spec | `docker/Dockerfile.opensuse-tumbleweed` |
| SLES 16 | `build-rpm.sh` | same spec | `docker/Dockerfile.sles-16` |
| Alpine | `build-apk.sh` | `apk/APKBUILD` (a completely different format/toolchain, `abuild` -- see python/packaging/README.md's Alpine section for the two things worth knowing) | `docker/Dockerfile.alpine` |

## Debian / Ubuntu

```bash
docker build -t multilang-go-deb -f packaging/docker/Dockerfile.debian-bookworm packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/go multilang-go-deb packaging/build-deb.sh
```

## RHEL / CentOS Stream / Fedora

```bash
docker build -t multilang-go-rpm -f packaging/docker/Dockerfile.fedora-latest packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/go multilang-go-rpm packaging/build-rpm.sh
```

## openSUSE (Leap 15, Tumbleweed) / SLES

```bash
docker build -t multilang-go-suse -f packaging/docker/Dockerfile.opensuse-tumbleweed packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/go multilang-go-suse packaging/build-rpm.sh
```

(swap the Dockerfile for `Dockerfile.opensuse-leap-15` — the spec is
identical either way).

## SLES 16

```bash
docker build -t multilang-go-sles -f packaging/docker/Dockerfile.sles-16 packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/go multilang-go-sles packaging/build-rpm.sh
```

Same spec, no distro branching. `docker/Dockerfile.sles-16` is `FROM
registry.suse.com/bci/bci-base:16.0` — SUSE's free, anonymously-pullable
image for SLE 16, pre-configured with the `SLE_BCI` repo so `zypper
install` works without an SCC registration/subscription.

## Alpine

```bash
docker build -t multilang-go-alpine -f packaging/docker/Dockerfile.alpine packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/go multilang-go-alpine packaging/build-apk.sh
```

See `python/packaging/README.md`'s Alpine section for why this build is
structurally different from the `.deb`/`.rpm` ones above (non-root
`builder` user + ephemeral signing key, `pkgver` read from an env var
instead of a command-line override). Ships as Go source under
`/usr/share/gocode/src/github.com/rmahique/multilang-lib/go/`, same
GOPATH-style layout as the Debian package — Alpine has no equivalent
official Go source-packaging policy, so this is this project's own
convention, kept consistent across formats.

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and this
project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly. The Alpine build's signing key is ephemeral
(see `docker/Dockerfile.alpine`) — fine for a CI artifact, not for a real
distributed repo.
