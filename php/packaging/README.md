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

`docker/` has a Dockerfile per distro with every build dependency baked
in (this is what `.github/workflows/build-packages.yml` builds and runs
in CI — same image, same commands, locally or in CI).

| Family | Script | Spec/control files | Dockerfile |
|---|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` | `docker/Dockerfile.debian-bookworm` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/php-multilang.spec` | `docker/Dockerfile.fedora-latest` |
| openSUSE Leap 15 | `build-rpm.sh` | same spec — no distro branching needed | `docker/Dockerfile.opensuse-leap-15` |
| openSUSE Tumbleweed | `build-rpm.sh` | same spec | `docker/Dockerfile.opensuse-tumbleweed` |
| SLES 16 | `build-rpm.sh` | same spec | `docker/Dockerfile.sles-16` |

## Debian / Ubuntu

```bash
docker build -t multilang-php-deb -f packaging/docker/Dockerfile.debian-bookworm packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/php multilang-php-deb packaging/build-deb.sh
```

## RHEL / CentOS Stream / Fedora

```bash
docker build -t multilang-php-rpm -f packaging/docker/Dockerfile.fedora-latest packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/php multilang-php-rpm packaging/build-rpm.sh
```

## openSUSE (Leap 15, Tumbleweed) / SLES

```bash
docker build -t multilang-php-suse -f packaging/docker/Dockerfile.opensuse-tumbleweed packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/php multilang-php-suse packaging/build-rpm.sh
```

(swap the Dockerfile for `Dockerfile.opensuse-leap-15` — the spec is
identical either way).

## SLES 16

```bash
docker build -t multilang-php-sles -f packaging/docker/Dockerfile.sles-16 packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/php multilang-php-sles packaging/build-rpm.sh
```

Same spec, no distro branching. `docker/Dockerfile.sles-16` is `FROM
registry.suse.com/bci/bci-base:16.0` — SUSE's free, anonymously-pullable
image for SLE 16, pre-configured with the `SLE_BCI` repo so `zypper
install` works without an SCC registration/subscription.

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and this
project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly.
