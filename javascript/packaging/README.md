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

| Family | Script | Spec/control files |
|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/nodejs-multilang.spec` |
| openSUSE Leap, Tumbleweed, SLES | `build-rpm.sh` | same spec — no distro branching needed |

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

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and this
project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly.
