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
| openSUSE Leap, SLES | `build-rpm.sh` | same spec — no distro branching needed | `docker/Dockerfile.opensuse-leap-15` |
| openSUSE Tumbleweed | `build-rpm.sh` | same spec | `docker/Dockerfile.opensuse-tumbleweed` |

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

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and this
project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly.
