# Packaging (JavaScript)

Ships CommonJS **source only**, wrapping this project's npm package — no
vendored `node_modules/`, no bundling. Node libraries aren't normally
distro-packaged (npm is the native distribution channel); these `.deb`/
`.rpm` packages are a pragmatic convenience for systems that want this
installed alongside other system packages rather than via `npm install`,
not a claim of full distro-policy compliance the way the C packaging is.

Database driver packages (`better-sqlite3`, `mysql2`, `pg`) are
deliberately **not** declared as package dependencies here — see the
comments in `debian/control` / `rpm/nodejs-multilang.spec`: they're
native-addon/npm-registry packages without a stable, verified name across
every distro this repo targets, so pulling in the wrong one via a system
package manager would be worse than leaving it to `npm install`.

`docker/` has a Dockerfile per distro with every build dependency baked
in (this is what `.github/workflows/build-packages.yml` builds and runs
in CI — same image, same commands, locally or in CI).

| Family | Script | Spec/control files | Dockerfile |
|---|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` | `docker/Dockerfile.debian-bookworm` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/nodejs-multilang.spec` | `docker/Dockerfile.fedora-latest` |
| openSUSE Leap 15 | `build-rpm.sh` | same spec — no distro branching needed | `docker/Dockerfile.opensuse-leap-15` |
| openSUSE Tumbleweed | `build-rpm.sh` | same spec | `docker/Dockerfile.opensuse-tumbleweed` |
| SLES 16 | `build-rpm.sh` | same spec | `docker/Dockerfile.sles-16` |
| Alpine | `build-apk.sh` | `apk/APKBUILD` (a completely different format/toolchain, `abuild` -- see python/packaging/README.md's Alpine section for the two things worth knowing) | `docker/Dockerfile.alpine` |

## Debian / Ubuntu

```bash
docker build -t multilang-js-deb -f packaging/docker/Dockerfile.debian-bookworm packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/javascript multilang-js-deb packaging/build-deb.sh
```

## RHEL / CentOS Stream / Fedora

```bash
docker build -t multilang-js-rpm -f packaging/docker/Dockerfile.fedora-latest packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/javascript multilang-js-rpm packaging/build-rpm.sh
```

## openSUSE (Leap 15, Tumbleweed) / SLES

```bash
docker build -t multilang-js-suse -f packaging/docker/Dockerfile.opensuse-tumbleweed packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/javascript multilang-js-suse packaging/build-rpm.sh
```

(swap the Dockerfile for `Dockerfile.opensuse-leap-15` — the spec is
identical either way).

## SLES 16

```bash
docker build -t multilang-js-sles -f packaging/docker/Dockerfile.sles-16 packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/javascript multilang-js-sles packaging/build-rpm.sh
```

Same spec, no distro branching. `docker/Dockerfile.sles-16` is `FROM
registry.suse.com/bci/bci-base:16.0` — SUSE's free, anonymously-pullable
image for SLE 16, pre-configured with the `SLE_BCI` repo so `zypper
install` works without an SCC registration/subscription.

## Alpine

```bash
docker build -t multilang-js-alpine -f packaging/docker/Dockerfile.alpine packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/javascript multilang-js-alpine packaging/build-apk.sh
```

See `python/packaging/README.md`'s Alpine section for why this build is
structurally different from the `.deb`/`.rpm` ones above (non-root
`builder` user + ephemeral signing key, `pkgver` read from an env var
instead of a command-line override). Ships as source under
`/usr/lib/node_modules/multilang/`, same as the Debian/RPM packages,
declaring no depends on the database driver packages for the same reason
those two give.

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and this
project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly. The Alpine build's signing key is ephemeral
(see `docker/Dockerfile.alpine`) — fine for a CI artifact, not for a real
distributed repo.
