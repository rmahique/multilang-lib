# Packaging (PHP)

Ships PSR-4 **source only**, wrapping this project's Composer package — no
vendored `vendor/`. PHP libraries aren't normally distro-packaged (Packagist
is the native distribution channel); these `.deb`/`.rpm` packages are a
pragmatic convenience, not a claim of full distro-policy compliance the way
the C packaging is.

The PDO driver needed for a given backend (SQLite/PostgreSQL/MySQL) is
deliberately **not** declared as a package dependency — see the comments in
`debian/control` / `rpm/php-multilang.spec`: exact package names for PHP
extensions vary by distro and PHP version (e.g. Debian's `php-pgsql` vs
openSUSE's `php8-pdo_pgsql`), so install whichever one your distro provides
yourself.

| Family | Script | Spec/control files |
|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/php-multilang.spec` |
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
