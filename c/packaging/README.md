# Packaging (C/C++)

The most standards-natural of this repo's five ports: a real compiled
shared-library split, `libmultilang0` (runtime: `libmultilang.so.0` +
`libmultilangxx.so.0`) and `libmultilang-dev`/`-devel` (headers +
unversioned `.so` symlinks for linking), built via `../Makefile`'s
`install`/`install-runtime`/`install-devel` targets.

**Dependency names deliberately aren't hardcoded** into either the
`.deb` control file or the RPM spec, since they differ across distros
(and the whole point of packaging every distro from one source tree is
not fighting that per distro by hand):

- Debian: `libmultilang0`'s `Depends: ${shlibs:Depends}` is computed by
  `dpkg-shlibdeps` from the actual linked `.so` files at build time.
- RPM: runtime `Requires` come from rpm's own automatic ELF-dependency
  generator (`find-requires`), which scans the built `.so`'s `NEEDED`
  entries. Build-time `BuildRequires` use `pkgconfig(sqlite3)`,
  `pkgconfig(libpq)`, `pkgconfig(openssl)`, `pkgconfig(libmariadb)` --
  virtual provides that resolve to whichever `-devel` package ships that
  `.pc` file on Fedora vs openSUSE, instead of guessing a package name.

The one place a package name *is* hardcoded is `debian/control`'s
`Build-Depends` (Debian-only, so there's exactly one name per library to
get right) and each CI job's own install step below -- if a distro
renames one of these, that job's install step is what needs updating,
not the control file or spec.

`docker/` has a Dockerfile per distro with every build dependency baked
in (this is what `.github/workflows/build-packages.yml` builds and runs
in CI — same image, same commands, locally or in CI).

| Family | Script | Spec/control files | Dockerfile |
|---|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` | `docker/Dockerfile.debian-bookworm` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/multilang.spec` | `docker/Dockerfile.fedora-latest` |
| openSUSE Leap, SLES | `build-rpm.sh` | same spec — no distro branching needed | `docker/Dockerfile.opensuse-leap-15` |
| openSUSE Tumbleweed | `build-rpm.sh` | same spec | `docker/Dockerfile.opensuse-tumbleweed` |

## Debian / Ubuntu

```bash
docker build -t multilang-c-deb -f packaging/docker/Dockerfile.debian-bookworm packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/c multilang-c-deb packaging/build-deb.sh
```

## RHEL / CentOS Stream / Fedora

```bash
docker build -t multilang-c-rpm -f packaging/docker/Dockerfile.fedora-latest packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/c multilang-c-rpm packaging/build-rpm.sh
```

## openSUSE (Leap 15, Tumbleweed) / SLES

```bash
docker build -t multilang-c-suse -f packaging/docker/Dockerfile.opensuse-tumbleweed packaging/docker
docker run --rm -v "$(pwd)/..":/workspace -w /workspace/c multilang-c-suse packaging/build-rpm.sh
```

(swap the Dockerfile for `Dockerfile.opensuse-leap-15` — the spec is
identical either way).

The Fedora/openSUSE package names baked into each Dockerfile
(`sqlite-devel` vs `sqlite3-devel`, `libpq-devel` vs `postgresql-devel`,
`mariadb-connector-c-devel` vs `libmariadb-devel`) are this project's own
best-effort guess for the *install step*, not something the spec itself
depends on getting right (see the dependency-names note above) -- if a
distro's package name has moved, only that Dockerfile needs updating.

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and
this project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly. Also note the built libraries install to
plain `/usr/lib` (`%{_libdir}` on RPM, so `/usr/lib64` where that's the
distro norm) rather than a Debian multiarch-triplet path
(`/usr/lib/x86_64-linux-gnu/`) -- fine for a single-arch CI build, but a
real multiarch-installable `.deb` would need that adjusted.
