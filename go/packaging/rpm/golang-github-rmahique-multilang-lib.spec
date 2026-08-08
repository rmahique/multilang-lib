%global goipath github.com/rmahique/multilang-lib/go
%global srcname golang-github-rmahique-multilang-lib

# build-rpm.sh passes --define "version ..."; 0.1.0 here is only a
# fallback for anyone invoking rpmbuild directly against this spec
# without going through the script.
%{!?version: %global version 0.1.0}

Name:           %{srcname}-devel
Version:        %{version}
Release:        1%{?dist}
Summary:        Reusable multi-language string storage library (Go source)

License:        GPL-3.0-or-later
URL:            https://github.com/rmahique/multilang-lib
Source0:        %{srcname}-%{version}.tar.gz

BuildArch:      noarch

# Source-only package, identical across every RPM-based distro (Fedora,
# RHEL, openSUSE Leap, openSUSE Tumbleweed) -- no compiled artifact, no
# distro-specific Go toolchain macros needed, so unlike the Python spec
# this one has no %%if 0%%{?suse_version} branches at all.

%description
Provides DBConnector, RetrieveData, and InsertData for storing and
retrieving translated strings, keyed by string_id + BCP 47 language_id,
against SQLite, PostgreSQL, or MySQL/MariaDB.

Ships as Go source under /usr/share/gocode/src/%{goipath}, per the same
GOPATH-style convention Debian's Go packaging policy uses -- there is no
compiled artifact here; consumers `go build`/`go get` against the
installed import path. Actual database driver dependencies
(github.com/lib/pq, github.com/go-sql-driver/mysql, modernc.org/sqlite)
are resolved by the consuming module's own go.mod, not by this package.

%prep
%autosetup -n %{srcname}-%{version}

%build
# nothing to build -- this package ships Go source, not a binary.

%install
mkdir -p %{buildroot}%{_datadir}/gocode/src/%{goipath}
cp -r ./*.go go.mod go.sum %{buildroot}%{_datadir}/gocode/src/%{goipath}/

%check
# go/'s own test suite runs in CI's go-unit job against a real Go
# toolchain and the full monorepo checkout (see
# ../../.github/workflows/ci.yml); this packaging build only needs to
# confirm the source tree installs to the right GOPATH layout, which
# %%install already does.

%files
%license LICENSE
%doc README.md
%{_datadir}/gocode/src/%{goipath}/

%changelog
* Thu Aug 06 2026 Raúl Mahiques <claude.ia@raulmahiques.com> - 0.1.0-1
- Initial release: Go source for DBConnector, RetrieveData, InsertData
  against SQLite, PostgreSQL, and MySQL/MariaDB.
