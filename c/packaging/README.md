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

| Family | Script | Spec/control files |
|---|---|---|
| Debian, Ubuntu | `build-deb.sh` | `debian/` |
| RHEL, CentOS Stream, Fedora | `build-rpm.sh` | `rpm/multilang.spec` |
| openSUSE Leap, Tumbleweed, SLES | `build-rpm.sh` | same spec — no distro branching needed |

## Debian / Ubuntu

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src debian:bookworm bash -c '
  apt-get update &&
  apt-get install -y build-essential debhelper pkg-config \
                      libsqlite3-dev libpq-dev default-libmysqlclient-dev \
                      libssl-dev git &&
  packaging/build-deb.sh
'
```

## RHEL / CentOS Stream / Fedora

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src fedora:latest bash -c '
  dnf install -y rpm-build gcc gcc-c++ make pkgconfig \
                  sqlite-devel libpq-devel openssl-devel \
                  mariadb-connector-c-devel git &&
  packaging/build-rpm.sh
'
```

## openSUSE (Leap 15, Tumbleweed) / SLES

```bash
docker run --rm -it -v "$(pwd)/..":/src -w /src opensuse/tumbleweed bash -c '
  zypper --non-interactive install rpm-build gcc gcc-c++ make pkg-config \
                                    sqlite3-devel postgresql-devel \
                                    libopenssl-devel libmariadb-devel git &&
  packaging/build-rpm.sh
'
```

The Fedora/openSUSE package names above (`sqlite-devel` vs
`sqlite3-devel`, `libpq-devel` vs `postgresql-devel`,
`mariadb-connector-c-devel` vs `libmariadb-devel`) are this script's own
best-effort guess for the *install step*, not something the spec itself
depends on getting right (see the dependency-names note above) -- if a
distro's package name has moved, only this command (and the matching CI
job) needs updating.

## Before a real release

Same as `python/packaging/README.md`: use `mock`/`sbuild`/`pbuilder` and
this project's normal OBS/COPR/PPA signing flow rather than `rpmbuild`/
`dpkg-buildpackage` directly. Also note the built libraries install to
plain `/usr/lib` (`%{_libdir}` on RPM, so `/usr/lib64` where that's the
distro norm) rather than a Debian multiarch-triplet path
(`/usr/lib/x86_64-linux-gnu/`) -- fine for a single-arch CI build, but a
real multiarch-installable `.deb` would need that adjusted.
